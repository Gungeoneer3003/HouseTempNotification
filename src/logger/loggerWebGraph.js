(function () {
    const root = document.getElementById("graphs");
    const todayRoot = document.getElementById("today");
    const colors = ["#22c55e", "#38bdf8", "#f59e0b", "#e879f9", "#f43f5e", "#a3e635"];
    const charts = [];
    let currentGraphs = [];
    let currentToday = null;
    let currentRange = "day";
    let currentRangeStart = null;
    let currentRangeEnd = null;
    let loadingGraphs = false;

    if (!root) {
        return;
    }

    const graphDataUrl = root.getAttribute("data-graph-data-url") || "/graphs/data";
    const graphRefreshMs = refreshMsFromAttribute(root.getAttribute("data-refresh-ms"));
    const showRefreshButton = root.getAttribute("data-show-refresh-button") === "1";

    function chartIsReady() {
        return typeof window.Chart === "function";
    }

    const eventMarkerPlugin = {
        id: "loggerEventMarkers",
        beforeDatasetsDraw(chart, args, options) {
            const events = options && Array.isArray(options.events) ? options.events : [];
            const spans = options && Array.isArray(options.spans) ? options.spans : [];
            if (events.length === 0 && spans.length === 0) {
                return;
            }

            const xScale = chart.scales.x;
            const area = chart.chartArea;
            if (!xScale || !area) {
                return;
            }

            const ctx = chart.ctx;
            ctx.save();
            spans.forEach((span) => {
                const startValue = finiteNumber(span.startTime);
                const endValue = finiteNumber(span.endTime);
                if (startValue === null || endValue === null || endValue <= startValue) {
                    return;
                }

                const startX = xScale.getPixelForValue(startValue);
                const endX = xScale.getPixelForValue(endValue);
                if (!Number.isFinite(startX) || !Number.isFinite(endX)) {
                    return;
                }

                const left = Math.min(startX, endX);
                const width = Math.abs(endX - startX);
                if (width <= 0) {
                    return;
                }

                const color = span.color || "#f59e0b";
                ctx.fillStyle = transparentColor(color, 0.14);
                ctx.fillRect(left, area.top, width, area.bottom - area.top);

                const label = span.duration || span.label || "";
                if (label && width >= 34) {
                    ctx.fillStyle = color;
                    ctx.font = "600 12px system-ui, -apple-system, Segoe UI, sans-serif";
                    ctx.textAlign = "center";
                    ctx.textBaseline = "top";
                    ctx.fillText(label, left + width / 2, area.top + 8);
                }
            });

            events.forEach((event) => {
                const timeValue = finiteNumber(event.time);
                if (timeValue === null) {
                    return;
                }

                const x = xScale.getPixelForValue(timeValue);
                if (!Number.isFinite(x)) {
                    return;
                }

                ctx.strokeStyle = event.color || "#ef4444";
                ctx.lineWidth = 1.5;
                ctx.setLineDash([5, 5]);
                ctx.beginPath();
                ctx.moveTo(x, area.top);
                ctx.lineTo(x, area.bottom);
                ctx.stroke();
            });
            ctx.restore();
        }
    };

    const timeAxisTicksPlugin = {
        id: "loggerTimeAxisTicks",
        afterBuildTicks(chart, args) {
            const scale = args && args.scale;
            if (!scale || scale.id !== "x") {
                return;
            }

            const ticks = buildAxisTicks(scale.min, scale.max);
            if (ticks.length > 0) {
                scale.ticks = ticks.map((value) => ({ value }));
            }
        }
    };

    async function loadGraphs() {
        if (!chartIsReady()) {
            renderError("Chart.js is unavailable.");
            return;
        }

        if (loadingGraphs) {
            return;
        }

        loadingGraphs = true;
        try {
            const response = await fetch(graphDataRequestUrl(), { cache: "no-store" });
            if (!response.ok) {
                throw new Error("graph data request failed");
            }

            const data = await response.json();
            currentRange = data.range === "week" ? "week" : "day";
            currentRangeStart = finiteNumber(data.rangeStartUnix);
            currentRangeEnd = finiteNumber(data.rangeEndUnix);
            currentToday = data.today || null;
            currentGraphs = Array.isArray(data.graphs) ? data.graphs : [];
            renderToday();
            renderGraphs();
        } catch (error) {
            renderToday(null);
            renderError("Graph data unavailable.");
        } finally {
            loadingGraphs = false;
        }
    }

    function graphDataRequestUrl() {
        const separator = graphDataUrl.includes("?") ? "&" : "?";
        return `${graphDataUrl}${separator}range=${encodeURIComponent(currentRange)}`;
    }

    function renderToday(value) {
        const today = arguments.length > 0 ? value : currentToday;
        if (!todayRoot) {
            return;
        }

        todayRoot.innerHTML = "";
        if (!today || !Array.isArray(today.columns) || today.columns.length === 0) {
            todayRoot.classList.add("is-hidden");
            return;
        }

        todayRoot.classList.remove("is-hidden");

        const heading = document.createElement("div");
        heading.className = "today-heading";
        heading.textContent = "Current readings";
        todayRoot.appendChild(heading);

        const readings = document.createElement("div");
        readings.className = "today-readings";
        today.columns.forEach((column) => {
            const item = document.createElement("div");
            item.className = "today-reading";

            const label = document.createElement("span");
            label.className = "today-label";
            label.textContent = column.name;
            item.appendChild(label);

            const number = document.createElement("span");
            number.className = "today-value";
            number.textContent = Number.isFinite(column.value) ? formatNumber(column.value) : "--";
            item.appendChild(number);
            readings.appendChild(item);
        });
        todayRoot.appendChild(readings);

        if (today.time) {
            const updated = document.createElement("div");
            updated.className = "today-updated";
            updated.textContent = `Updated ${today.time}`;
            todayRoot.appendChild(updated);
        }
    }

    function renderGraphs() {
        destroyCharts();
        root.innerHTML = "";

        if (currentGraphs.length === 0) {
            root.appendChild(emptyMessage("No graphs configured."));
            return;
        }

        currentGraphs.forEach((graph) => {
            root.appendChild(createGraphSection(graph));
        });
    }

    function createGraphSection(graph) {
        const section = document.createElement("section");
        section.className = "graph";

        const header = document.createElement("div");
        header.className = "graph-header";

        const title = document.createElement("h2");
        title.textContent = graph.title || "Graph";
        header.appendChild(title);
        header.appendChild(createGraphActions());
        section.appendChild(header);

        const chartWrap = document.createElement("div");
        chartWrap.className = "chart-wrap";

        if (hasGraphPoints(graph)) {
            const canvas = document.createElement("canvas");
            canvas.className = "chart";
            canvas.setAttribute("role", "img");
            canvas.setAttribute("aria-label", graph.title || "Graph");
            chartWrap.appendChild(canvas);
            section.appendChild(chartWrap);

            createChart(canvas, graph);
        } else {
            chartWrap.appendChild(emptyMessage("No data for selected range."));
            section.appendChild(chartWrap);
        }

        const stats = createStatsBox(graph);
        if (stats) {
            section.appendChild(stats);
        }

        return section;
    }

    function createGraphActions() {
        const actions = document.createElement("div");
        actions.className = "graph-actions";

        if (showRefreshButton) {
            actions.appendChild(actionButton("Refresh", () => {
                loadGraphs();
            }));
        }
        actions.appendChild(rangeButton("day", "Show Day"));
        actions.appendChild(rangeButton("week", "Show Week"));

        return actions;
    }

    function rangeButton(range, label) {
        return actionButton(label, () => {
            currentRange = range;
            loadGraphs();
        }, currentRange === range);
    }

    function actionButton(label, onClick, active) {
        const button = document.createElement("button");
        button.type = "button";
        button.className = active ? "graph-button is-active" : "graph-button";
        button.textContent = label;
        if (typeof active === "boolean") {
            button.setAttribute("aria-pressed", active ? "true" : "false");
        }
        button.addEventListener("click", onClick);
        return button;
    }

    function createChart(canvas, graph) {
        const points = graphPoints(graph);
        const bounds = xAxisBounds(points);
        const datasets = (graph.series || []).map((series, seriesIndex) => ({
            label: series.name || "Series",
            data: points.map((point) => {
                const value = point.values ? point.values[seriesIndex] : null;
                return {
                    x: point.time,
                    y: Number.isFinite(value) ? value : null,
                    label: point.x || formatGraphDateTime(point.time)
                };
            }),
            borderColor: colorForSeries(seriesIndex),
            backgroundColor: transparentColor(colorForSeries(seriesIndex), 0.12),
            borderWidth: 2.75,
            cubicInterpolationMode: "default",
            tension: 0.48,
            pointRadius: 0,
            pointHitRadius: 12,
            pointHoverRadius: 4,
            spanGaps: true
        }));

        const chart = new Chart(canvas, {
            type: "line",
            data: {
                datasets
            },
            plugins: [timeAxisTicksPlugin, eventMarkerPlugin],
            options: {
                responsive: true,
                maintainAspectRatio: false,
                normalized: true,
                parsing: false,
                animation: {
                    duration: 300
                },
                interaction: {
                    intersect: false,
                    mode: "index"
                },
                elements: {
                    line: {
                        borderCapStyle: "round",
                        borderJoinStyle: "round"
                    }
                },
                plugins: {
                    loggerEventMarkers: {
                        events: Array.isArray(graph.events) ? graph.events : [],
                        spans: Array.isArray(graph.spans) ? graph.spans : []
                    },
                    legend: {
                        labels: {
                            color: "#cbd5e1",
                            usePointStyle: true,
                            boxWidth: 8,
                            boxHeight: 8
                        }
                    },
                    tooltip: {
                        backgroundColor: "#020617",
                        borderColor: "#334155",
                        borderWidth: 1,
                        titleColor: "#f8fafc",
                        bodyColor: "#e5e7eb",
                        displayColors: true,
                        callbacks: {
                            title: function (items) {
                                const item = items && items.length > 0 ? items[0] : null;
                                const raw = item ? item.raw : null;
                                return raw && Number.isFinite(raw.x) ? formatGraphDateTime(raw.x) : "";
                            }
                        }
                    }
                },
                scales: {
                    x: {
                        type: "linear",
                        min: bounds.min,
                        max: bounds.max,
                        grid: {
                            color: "rgba(31, 41, 55, 0.55)"
                        },
                        ticks: {
                            color: "#cbd5e1",
                            maxRotation: 0,
                            autoSkip: false,
                            maxTicksLimit: currentRange === "week" ? 8 : 13,
                            stepSize: currentRange === "week" ? 24 * 60 * 60 : 2 * 60 * 60,
                            callback: function (value) {
                                return formatAxisTick(value);
                            }
                        },
                        title: {
                            display: true,
                            text: currentRange === "week" ? "Day" : (graph.xColumn || "Time"),
                            color: "#cbd5e1"
                        }
                    },
                    y: {
                        grid: {
                            color: "rgba(38, 51, 69, 0.72)"
                        },
                        ticks: {
                            color: "#cbd5e1"
                        },
                        title: {
                            display: true,
                            text: axisTitle(graph),
                            color: "#cbd5e1"
                        }
                    }
                }
            }
        });

        charts.push(chart);
    }

    function createStatsBox(graph) {
        const stats = graph.stats;
        if (!stats || !Array.isArray(stats.series) || stats.series.length === 0) {
            return null;
        }

        const box = document.createElement("div");
        box.className = "stats-box";

        const title = document.createElement("div");
        title.className = "stats-title";
        title.textContent = "Today's Stats";
        box.appendChild(title);

        const grid = document.createElement("div");
        grid.className = "stats-grid";
        stats.series.forEach((item, index) => {
            const card = document.createElement("div");
            card.className = "stat-card";
            card.style.borderColor = transparentColor(colorForSeries(index), 0.56);

            const name = document.createElement("div");
            name.className = "stat-name";
            name.textContent = item.name;
            card.appendChild(name);

            const values = document.createElement("div");
            values.className = "stat-values";
            values.appendChild(statValue("Min", item.min));
            values.appendChild(statValue("Max", item.max));
            card.appendChild(values);
            grid.appendChild(card);
        });
        box.appendChild(grid);
        return box;
    }

    function statValue(label, value) {
        const item = document.createElement("div");
        item.className = "stat-value";

        const labelElement = document.createElement("span");
        labelElement.textContent = label;
        item.appendChild(labelElement);

        const valueElement = document.createElement("strong");
        valueElement.textContent = Number.isFinite(value) ? formatNumber(value) : "--";
        item.appendChild(valueElement);
        return item;
    }

    function destroyCharts() {
        while (charts.length > 0) {
            charts.pop().destroy();
        }
    }

    function renderError(message) {
        destroyCharts();
        root.innerHTML = "";
        const section = document.createElement("section");
        section.className = "graph";

        const header = document.createElement("div");
        header.className = "graph-header";

        const title = document.createElement("h2");
        title.textContent = "Graphs";
        header.appendChild(title);
        header.appendChild(createGraphActions());

        section.appendChild(header);
        section.appendChild(emptyMessage(message));
        root.appendChild(section);
    }

    function axisTitle(graph) {
        const names = (graph.series || []).map((series) => series.name).filter(Boolean);
        return names.length <= 1 ? (names[0] || "Y") : "Temperature";
    }

    function hasGraphPoints(graph) {
        return graphPoints(graph).length > 0;
    }

    function graphPoints(graph) {
        return (graph.points || []).map((point) => {
            const time = finiteNumber(point.time);
            if (time === null) {
                return null;
            }

            return {
                x: point.x,
                time,
                values: point.values
            };
        }).filter(Boolean);
    }

    function xAxisBounds(points) {
        const rangeStart = finiteNumber(currentRangeStart);
        const rangeEnd = finiteNumber(currentRangeEnd);
        if (rangeStart !== null && rangeEnd !== null && rangeEnd > rangeStart) {
            return { min: rangeStart, max: rangeEnd };
        }

        const values = points.map((point) => point.time).filter(Number.isFinite);
        if (values.length === 0) {
            const now = Math.floor(Date.now() / 1000);
            return { min: now - 24 * 60 * 60, max: now };
        }

        const min = Math.min.apply(null, values);
        const max = Math.max.apply(null, values);
        return max > min ? { min, max } : { min: min - 60 * 60, max: max + 60 * 60 };
    }

    function buildAxisTicks(min, max) {
        const rangeStart = finiteNumber(currentRangeStart);
        const rangeEnd = finiteNumber(currentRangeEnd);
        const start = rangeStart !== null ? rangeStart : finiteNumber(min);
        const end = rangeEnd !== null ? rangeEnd : finiteNumber(max);
        if (start === null || end === null || end <= start) {
            return [];
        }

        return currentRange === "week"
            ? buildDailyTicks(start, end)
            : buildHourlyTicks(start, end);
    }

    function buildHourlyTicks(start, end) {
        const ticks = [];
        const tick = new Date(start * 1000);
        tick.setMinutes(0, 0, 0);
        if (tick.getHours() % 2 !== 0) {
            tick.setHours(tick.getHours() - 1);
        }
        if (tick.getTime() / 1000 < start) {
            tick.setHours(tick.getHours() + 2);
        }

        while (tick.getTime() / 1000 <= end) {
            ticks.push(tick.getTime() / 1000);
            tick.setHours(tick.getHours() + 2);
        }

        return ticks;
    }

    function buildDailyTicks(start, end) {
        const ticks = [];
        const base = new Date(start * 1000);
        base.setHours(0, 0, 0, 0);

        for (let day = 0; day < 7; day++) {
            const tick = new Date(base.getTime());
            tick.setDate(base.getDate() + day);
            const value = tick.getTime() / 1000;
            if (value >= start && value < end) {
                ticks.push(value);
            }
        }

        return ticks;
    }

    function finiteNumber(value) {
        const number = Number(value);
        return Number.isFinite(number) ? number : null;
    }

    function refreshMsFromAttribute(value) {
        const refreshMs = Number(value);
        return Number.isFinite(refreshMs) && refreshMs > 0 ? refreshMs : 150000;
    }

    function formatAxisTick(value) {
        const seconds = finiteNumber(value);
        if (seconds === null) {
            return "";
        }

        return currentRange === "week" ? formatWeekday(seconds) : formatHour(seconds);
    }

    function formatGraphDateTime(seconds) {
        const options = currentRange === "week"
            ? { weekday: "long", hour: "numeric", minute: "2-digit", hour12: true }
            : { month: "short", day: "numeric", hour: "numeric", minute: "2-digit", hour12: true };
        return formatDate(seconds, options);
    }

    function formatHour(seconds) {
        return formatDate(seconds, { hour: "numeric", hour12: true });
    }

    function formatWeekday(seconds) {
        return formatDate(seconds, { weekday: "long" });
    }

    function formatDate(seconds, options) {
        const date = new Date(seconds * 1000);
        return Number.isFinite(date.getTime()) ? date.toLocaleString(undefined, options) : "";
    }

    function colorForSeries(index) {
        return colors[index % colors.length];
    }

    function transparentColor(hex, alpha) {
        const value = hex.replace("#", "");
        const red = parseInt(value.slice(0, 2), 16);
        const green = parseInt(value.slice(2, 4), 16);
        const blue = parseInt(value.slice(4, 6), 16);
        return `rgba(${red}, ${green}, ${blue}, ${alpha})`;
    }

    function formatNumber(value) {
        return Math.abs(value) >= 100 ? value.toFixed(0) : value.toFixed(1);
    }

    function emptyMessage(message) {
        const element = document.createElement("p");
        element.className = "empty";
        element.textContent = message;
        return element;
    }

    loadGraphs();
    if (!showRefreshButton) {
        window.setInterval(loadGraphs, graphRefreshMs);
    }
}());
