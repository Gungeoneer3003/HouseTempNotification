(function () {
    let controlRequestInFlight = false;
    const reloadDelayMs = 1200;

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
        const previousState = readTodayControlState(controls);
        controlRequestInFlight = true;
        setTodayControlsDisabled(controls, true);
        button.classList.remove("today-control-error");

        // Show the expected result right away; the physical controller is polled after it settles.
        writeTodayControlState(controls, nextTodayControlState(endpoint, controls, previousState));

        let requestSucceeded = false;
        try {
            const response = await fetch(endpoint, {
                method: "POST",
                cache: "no-store"
            });
            if (!response.ok) {
                throw new Error("today control request failed");
            }

            requestSucceeded = true;
            window.setTimeout(function () {
                window.location.reload();
            }, reloadDelayMs);
        } catch (error) {
            writeTodayControlState(controls, previousState);
            button.classList.add("today-control-error");
            window.setTimeout(function () {
                button.classList.remove("today-control-error");
            }, 1400);
        } finally {
            if (!requestSucceeded) {
                controlRequestInFlight = false;
                setTodayControlsDisabled(controls, false);
            }
        }
    });

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

    function nextTodayControlState(endpoint, controls, state) {
        const current = state || { speed: 0, powerOn: false };
        let speed = current.speed;
        let powerOn = current.powerOn;

        if (endpoint.endsWith("/speed/up")) {
            speed += 1;
            powerOn = speed > 0;
        } else if (endpoint.endsWith("/speed/down")) {
            speed = Math.max(0, speed - 1);
            powerOn = speed > 0;
        } else if (endpoint.endsWith("/power/toggle")) {
            powerOn = !powerOn;
            speed = powerOn ? Math.max(speed, powerOnSpeed(controls)) : 0;
        }

        return { speed, powerOn };
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

    function powerOnSpeed(controls) {
        const configured = controls ? Number(controls.getAttribute("data-power-on-speed")) : 0;
        return Number.isFinite(configured) && configured > 0 ? Math.round(configured) : 1;
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
