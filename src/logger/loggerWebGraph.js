(function () {
    const root = document.getElementById("graphs");
    const todayRoot = document.getElementById("today");
    const colors = ["#22c55e", "#38bdf8", "#f59e0b", "#e879f9", "#f43f5e", "#a3e635"];
    const charts = [];
    let currentGraphs = [];
    let currentToday = null;

    if (!root) {
        return;
    }

    function chartIsReady() {
        return typeof window.Chart === "function";
    }

    const eventMarkerPlugin = {
        id: "loggerEventMarkers",
        beforeDatasetsDraw(chart, args, options) {
            const events = options && Array.isArray(options.events) ? options.events : [];
            if (events.length === 0) {
                return;
            }

            const xScale = chart.scales.x;
            const area = chart.chartArea;
            if (!xScale || !area) {
                return;
            }

            const labels = chart.data.labels || [];
            const ctx = chart.ctx;
            ctx.save();
            events.forEach((event) => {
                const index = labels.indexOf(event.x);
                if (index < 0) {
                    return;
                }

                const x = xScale.getPixelForValue(index);
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

    async function loadGraphs() {
        if (!chartIsReady()) {
            renderError("Chart.js is unavailable.");
            return;
        }

        try {
            const response = await fetch("/graphs/data", { cache: "no-store" });
            if (!response.ok) {
                throw new Error("graph data request failed");
            }

            const data = await response.json();
            currentToday = data.today || null;
            currentGraphs = Array.isArray(data.graphs) ? data.graphs : [];
            renderToday();
            renderGraphs();
        } catch (error) {
            renderToday(null);
            renderError("Graph data unavailable.");
        }
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

        const title = document.createElement("h2");
        title.textContent = graph.title || "Graph";
        section.appendChild(title);

        const chartWrap = document.createElement("div");
        chartWrap.className = "chart-wrap";

        const canvas = document.createElement("canvas");
        canvas.className = "chart";
        canvas.setAttribute("role", "img");
        canvas.setAttribute("aria-label", graph.title || "Graph");
        chartWrap.appendChild(canvas);
        section.appendChild(chartWrap);

        createChart(canvas, graph);

        const stats = createStatsBox(graph);
        if (stats) {
            section.appendChild(stats);
        }

        return section;
    }

    function createChart(canvas, graph) {
        const labels = (graph.points || []).map((point) => point.x);
        const datasets = (graph.series || []).map((series, seriesIndex) => ({
            label: series.name || "Series",
            data: (graph.points || []).map((point) => {
                const value = point.values ? point.values[seriesIndex] : null;
                return Number.isFinite(value) ? value : null;
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
                labels,
                datasets
            },
            plugins: [eventMarkerPlugin],
            options: {
                responsive: true,
                maintainAspectRatio: false,
                normalized: true,
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
                        events: Array.isArray(graph.events) ? graph.events : []
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
                        displayColors: true
                    }
                },
                scales: {
                    x: {
                        grid: {
                            color: "rgba(31, 41, 55, 0.55)"
                        },
                        ticks: {
                            color: "#cbd5e1",
                            maxRotation: 0,
                            autoSkip: true,
                            maxTicksLimit: 8,
                            callback: function (value) {
                                return shortTime(this.getLabelForValue(value));
                            }
                        },
                        title: {
                            display: true,
                            text: graph.xColumn || "Time",
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
        title.textContent = stats.windowStart && stats.windowEnd
            ? `Last 24 hours (${shortDate(stats.windowStart)} - ${shortDate(stats.windowEnd)})`
            : "Last 24 hours";
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
        root.appendChild(emptyMessage(message));
    }

    function axisTitle(graph) {
        const names = (graph.series || []).map((series) => series.name).filter(Boolean);
        return names.length <= 1 ? (names[0] || "Y") : "Temperature";
    }

    function shortTime(label) {
        if (typeof label !== "string") {
            return label;
        }

        const match = label.match(/\b(\d{1,2}:\d{2})(?::\d{2})?\b/);
        return match ? match[1] : label;
    }

    function shortDate(label) {
        if (typeof label !== "string") {
            return "";
        }

        const match = label.match(/^(\d{4}-\d{2}-\d{2})/);
        return match ? match[1] : label;
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
    setInterval(loadGraphs, 30000);
}());