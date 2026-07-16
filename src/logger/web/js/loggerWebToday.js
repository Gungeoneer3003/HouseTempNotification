(function () {
    let controlRequestInFlight = false;

    //Today control buttons post to explicit endpoints so simple page loads stay read-only.
    document.addEventListener("click", async function (event) {
        const button = event.target.closest("[data-today-endpoint]");
        if (!button || button.disabled || controlRequestInFlight) {
            return;
        }

        event.preventDefault();
        const endpoint = button.getAttribute("data-today-endpoint");
        if (!endpoint) {
            return;
        }

        const controls = button.closest(".today-controls");
        const todayPanel = button.closest("#today");
        const previousState = readTodayControlState(controls);
        controlRequestInFlight = true;
        setTodayControlsDisabled(controls, true);
        button.classList.remove("today-control-error");
        writeControlMessage(
            todayPanel,
            controlProgressMessage(endpoint, controls, previousState),
            "pending"
        );

        try {
            const response = await fetch(endpoint, {
                method: "POST",
                cache: "no-store"
            });
            if (!response.ok) {
                const detail = (await response.text()).trim();
                throw new Error(detail || "The fan controller rejected the request.");
            }

            const snapshot = await response.json();
            if (!snapshot || !snapshot.controls ||
                !Number.isFinite(Number(snapshot.controls.fanSpeed))) {
                throw new Error("today control response was missing final state");
            }

            writeTodayControlState(controls, {
                speed: Number(snapshot.controls.fanSpeed),
                powerOn: !!snapshot.controls.fanPowerOn
            });
            writeTodayStatus(todayPanel, snapshot.status);
            writeControlMessage(
                todayPanel,
                controlSuccessMessage(endpoint, snapshot.controls),
                "success"
            );
        } catch (error) {
            writeTodayControlState(controls, previousState);
            button.classList.add("today-control-error");
            writeControlMessage(
                todayPanel,
                `Fan control failed: ${error && error.message
                    ? error.message
                    : "the controller did not respond."}`,
                "error"
            );
            window.setTimeout(function () {
                button.classList.remove("today-control-error");
            }, 1400);
        } finally {
            controlRequestInFlight = false;
            setTodayControlsDisabled(controls, false);
        }
    });

    function controlProgressMessage(endpoint, controls, state) {
        if (endpoint.endsWith("/power/toggle")) {
            if (!state || !state.powerOn) {
                const target = Math.max(1, Math.round(numberFromText(
                    controls ? controls.getAttribute("data-power-on-speed") : null,
                    null
                )));
                return `Opening the fan shutters, then setting fan speed to ${target}. ` +
                    "This normally takes at least 10 seconds…";
            }
            return "Turning the fan off and waiting for the controller…";
        }

        return endpoint.endsWith("/speed/up")
            ? "Increasing fan speed and checking the final setting…"
            : "Decreasing fan speed and checking the final setting…";
    }

    function controlSuccessMessage(endpoint, controls) {
        const speed = Math.max(0, Math.round(Number(controls.fanSpeed)));
        if (endpoint.endsWith("/power/toggle")) {
            return controls.fanPowerOn
                ? `Fan shutters are open and the fan is running at speed ${speed}.`
                : "The fan is off.";
        }
        return `Fan speed is now ${speed}.`;
    }

    function writeControlMessage(todayPanel, message, state) {
        if (!todayPanel || !message) {
            return;
        }

        let box = todayPanel.querySelector(".today-control-message");
        if (!box) {
            box = document.createElement("div");
            box.className = "today-control-message";
            box.setAttribute("role", "status");
            box.setAttribute("aria-live", "polite");
            todayPanel.appendChild(box);
        }

        box.className = `today-control-message is-${state}`;
        box.textContent = message;
    }

    function writeTodayStatus(todayPanel, status) {
        if (!todayPanel || typeof status !== "string" || status.length === 0) {
            return;
        }

        const statusBox = todayPanel.querySelector(".today-status");
        if (statusBox) {
            statusBox.textContent = `Status: ${status}`;
        }
    }

    function setTodayControlsDisabled(controls, disabled) {
        if (!controls) {
            return;
        }

        controls.classList.toggle("is-pending", disabled);
        controls.querySelectorAll("[data-today-endpoint]").forEach(function (button) {
            if (disabled) {
                // Remember which buttons were disabled only because this request is in flight.
                if (!button.disabled) {
                    button.setAttribute("data-busy-disabled", "1");
                }
                button.disabled = true;
                return;
            }

            if (button.getAttribute("data-busy-disabled") === "1") {
                button.disabled = false;
                button.removeAttribute("data-busy-disabled");
            }
        });
    }

    function readTodayControlState(controls) {
        if (!controls) {
            return null;
        }

        const speed = numberFromText(
            controls.getAttribute("data-fan-speed"),
            controls.querySelector(".today-speed-readout .today-value")
        );
        return {
            speed: Math.max(0, Math.round(speed)),
            powerOn: controls.getAttribute("data-fan-power-on") === "1" || speed > 0
        };
    }

    function writeTodayControlState(controls, state) {
        if (!controls || !state) {
            return;
        }

        const speed = Math.max(0, Math.round(state.speed));
        controls.setAttribute("data-fan-speed", String(speed));
        controls.setAttribute("data-fan-power-on", state.powerOn ? "1" : "0");

        const speedValue = controls.querySelector(".today-speed-readout .today-value");
        if (speedValue) {
            speedValue.textContent = String(speed);
        }

        const powerButton = controls.querySelector(".today-power-control");
        if (powerButton) {
            powerButton.classList.toggle("is-on", !!state.powerOn);
            powerButton.classList.toggle("is-off", !state.powerOn);
            powerButton.setAttribute("aria-pressed", state.powerOn ? "true" : "false");
        }
    }

    function numberFromText(attributeValue, element) {
        const fromAttribute = Number(attributeValue);
        if (Number.isFinite(fromAttribute)) {
            return fromAttribute;
        }

        const fromElement = element ? Number(element.textContent) : 0;
        return Number.isFinite(fromElement) ? fromElement : 0;
    }
}());
