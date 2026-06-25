(function () {
    const root = document.getElementById("graphs");
    const colors = ["#22c55e", "#38bdf8", "#f59e0b", "#e879f9", "#f43f5e", "#a3e635"];
    let currentGraphs = [];
    let resizeTimer = 0;

    if (!root) {
        return;
    }

    async function loadGraphs() {
        try {
            const response = await fetch("/graphs/data", { cache: "no-store" });
            if (!response.ok) {
                throw new Error("graph data request failed");
            }

            const data = await response.json();
            currentGraphs = Array.isArray(data.graphs) ? data.graphs : [];
            renderGraphs();
        } catch (error) {
            root.innerHTML = "";
            root.appendChild(emptyMessage("Graph data unavailable."));
        }
    }

    function renderGraphs() {
        root.innerHTML = "";

        if (currentGraphs.length === 0) {
            root.appendChild(emptyMessage("No graphs configured."));
            return;
        }

        currentGraphs.forEach((graph, index) => {
            root.appendChild(createGraphSection(graph, index));
        });

        drawAllGraphs();
    }

    function createGraphSection(graph, index) {
        const section = document.createElement("section");
        section.className = "graph";

        const title = document.createElement("h2");
        title.textContent = graph.title || "Graph";
        section.appendChild(title);

        const legend = document.createElement("div");
        legend.className = "legend";
        (graph.series || []).forEach((series, seriesIndex) => {
            const item = document.createElement("span");
            item.className = "legend-item";

            const swatch = document.createElement("span");
            swatch.className = "legend-swatch";
            swatch.style.backgroundColor = colorForSeries(seriesIndex);
            item.appendChild(swatch);

            const label = document.createElement("span");
            label.textContent = series.name || "Series";
            item.appendChild(label);
            legend.appendChild(item);
        });
        section.appendChild(legend);

        const canvas = document.createElement("canvas");
        canvas.className = "chart";
        canvas.dataset.graphIndex = String(index);
        canvas.setAttribute("role", "img");
        canvas.setAttribute("aria-label", graph.title || "Graph");
        section.appendChild(canvas);

        return section;
    }

    function drawAllGraphs() {
        const canvases = root.querySelectorAll("canvas[data-graph-index]");
        canvases.forEach((canvas) => {
            const index = Number(canvas.dataset.graphIndex);
            drawGraph(canvas, currentGraphs[index]);
        });
    }

    function drawGraph(canvas, graph) {
        if (!canvas || !graph) {
            return;
        }

        const width = Math.max(canvas.clientWidth || 720, 320);
        const height = Math.max(canvas.clientHeight || 320, 280);
        const scale = window.devicePixelRatio || 1;
        canvas.width = Math.round(width * scale);
        canvas.height = Math.round(height * scale);

        const ctx = canvas.getContext("2d");
        ctx.setTransform(scale, 0, 0, scale, 0, 0);
        ctx.clearRect(0, 0, width, height);

        const points = Array.isArray(graph.points) ? graph.points : [];
        const series = Array.isArray(graph.series) ? graph.series : [];
        const values = collectValues(points);
        if (points.length === 0 || series.length === 0 || values.length === 0) {
            drawEmptyChart(ctx, width, height, "No numeric data found.");
            return;
        }

        const bounds = valueBounds(values);
        const plot = {
            left: 58,
            top: 20,
            right: width - 20,
            bottom: height - 52
        };
        plot.width = plot.right - plot.left;
        plot.height = plot.bottom - plot.top;

        drawGrid(ctx, plot, bounds);
        drawSeries(ctx, plot, points, series, bounds);
        drawAxes(ctx, plot, points, graph, bounds);
    }

    function collectValues(points) {
        const values = [];
        points.forEach((point) => {
            (point.values || []).forEach((value) => {
                if (Number.isFinite(value)) {
                    values.push(value);
                }
            });
        });
        return values;
    }

    function valueBounds(values) {
        let min = Math.min(...values);
        let max = Math.max(...values);
        if (min === max) {
            min -= 1;
            max += 1;
        } else {
            const padding = (max - min) * 0.08;
            min -= padding;
            max += padding;
        }
        return { min, max };
    }

    function drawGrid(ctx, plot, bounds) {
        ctx.save();
        ctx.lineWidth = 1;
        ctx.strokeStyle = "#263345";
        ctx.fillStyle = "#cbd5e1";
        ctx.font = "12px system-ui, -apple-system, Segoe UI, sans-serif";
        ctx.textAlign = "right";
        ctx.textBaseline = "middle";

        for (let tick = 0; tick <= 4; tick++) {
            const y = plot.top + (plot.height * tick / 4);
            const value = bounds.max - ((bounds.max - bounds.min) * tick / 4);
            ctx.beginPath();
            ctx.moveTo(plot.left, y);
            ctx.lineTo(plot.right, y);
            ctx.stroke();
            ctx.fillText(formatNumber(value), plot.left - 10, y);
        }

        ctx.strokeStyle = "#94a3b8";
        ctx.beginPath();
        ctx.moveTo(plot.left, plot.top);
        ctx.lineTo(plot.left, plot.bottom);
        ctx.lineTo(plot.right, plot.bottom);
        ctx.stroke();
        ctx.restore();
    }

    function drawSeries(ctx, plot, points, series, bounds) {
        series.forEach((item, seriesIndex) => {
            const color = colorForSeries(seriesIndex);
            const linePoints = points.map((point, pointIndex) => {
                const value = point.values ? point.values[seriesIndex] : null;
                if (!Number.isFinite(value)) {
                    return null;
                }

                return {
                    x: xPosition(plot, pointIndex, points.length),
                    y: yPosition(plot, value, bounds)
                };
            });

            drawLineSegments(ctx, linePoints, color);
            drawLatestPoint(ctx, linePoints, color);
        });
    }

    function drawLineSegments(ctx, points, color) {
        let segment = [];
        points.forEach((point) => {
            if (point) {
                segment.push(point);
            } else {
                drawSmoothSegment(ctx, segment, color);
                segment = [];
            }
        });
        drawSmoothSegment(ctx, segment, color);
    }

    function drawSmoothSegment(ctx, points, color) {
        if (points.length === 0) {
            return;
        }

        ctx.save();
        ctx.strokeStyle = color;
        ctx.fillStyle = color;
        ctx.lineWidth = 2.5;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        if (points.length === 1) {
            ctx.beginPath();
            ctx.arc(points[0].x, points[0].y, 3, 0, Math.PI * 2);
            ctx.fill();
            ctx.restore();
            return;
        }

        ctx.beginPath();
        ctx.moveTo(points[0].x, points[0].y);
        for (let i = 1; i < points.length - 1; i++) {
            const midX = (points[i].x + points[i + 1].x) / 2;
            const midY = (points[i].y + points[i + 1].y) / 2;
            ctx.quadraticCurveTo(points[i].x, points[i].y, midX, midY);
        }
        const last = points[points.length - 1];
        ctx.lineTo(last.x, last.y);
        ctx.stroke();
        ctx.restore();
    }

    function drawLatestPoint(ctx, points, color) {
        for (let i = points.length - 1; i >= 0; i--) {
            if (!points[i]) {
                continue;
            }

            ctx.save();
            ctx.fillStyle = color;
            ctx.strokeStyle = "#111827";
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.arc(points[i].x, points[i].y, 4, 0, Math.PI * 2);
            ctx.fill();
            ctx.stroke();
            ctx.restore();
            return;
        }
    }

    function drawAxes(ctx, plot, points, graph, bounds) {
        ctx.save();
        ctx.fillStyle = "#cbd5e1";
        ctx.font = "12px system-ui, -apple-system, Segoe UI, sans-serif";
        ctx.textBaseline = "top";
        ctx.textAlign = "left";
        ctx.fillText(points[0].x, plot.left, plot.bottom + 10);
        ctx.textAlign = "right";
        ctx.fillText(points[points.length - 1].x, plot.right, plot.bottom + 10);

        ctx.textAlign = "center";
        ctx.font = "600 12px system-ui, -apple-system, Segoe UI, sans-serif";
        ctx.fillText(graph.xColumn || "X", plot.left + plot.width / 2, plot.bottom + 32);

        ctx.translate(14, plot.top + plot.height / 2);
        ctx.rotate(-Math.PI / 2);
        ctx.fillText(axisTitle(graph), 0, 0);
        ctx.restore();

        void bounds;
    }

    function drawEmptyChart(ctx, width, height, message) {
        ctx.save();
        ctx.fillStyle = "#cbd5e1";
        ctx.font = "14px system-ui, -apple-system, Segoe UI, sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(message, width / 2, height / 2);
        ctx.restore();
    }

    function xPosition(plot, index, count) {
        if (count <= 1) {
            return plot.left + plot.width / 2;
        }
        return plot.left + (plot.width * index / (count - 1));
    }

    function yPosition(plot, value, bounds) {
        return plot.top + ((bounds.max - value) * plot.height / (bounds.max - bounds.min));
    }

    function axisTitle(graph) {
        const names = (graph.series || []).map((series) => series.name).filter(Boolean);
        return names.length <= 1 ? (names[0] || "Y") : "Temperature";
    }

    function colorForSeries(index) {
        return colors[index % colors.length];
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

    window.addEventListener("resize", () => {
        clearTimeout(resizeTimer);
        resizeTimer = setTimeout(drawAllGraphs, 100);
    });

    loadGraphs();
    setInterval(loadGraphs, 30000);
}());
