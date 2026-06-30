(function () {
    //Today control buttons post to explicit endpoints so simple page loads stay read-only.
    document.addEventListener("click", async function (event) {
        const button = event.target.closest("[data-today-endpoint]");
        if (!button || button.disabled) {
            return;
        }

        event.preventDefault();
        const endpoint = button.getAttribute("data-today-endpoint");
        if (!endpoint) {
            return;
        }

        button.disabled = true;
        button.classList.remove("today-control-error");
        try {
            const response = await fetch(endpoint, {
                method: "POST",
                cache: "no-store"
            });
            if (!response.ok) {
                throw new Error("today control request failed");
            }

            window.location.reload();
        } catch (error) {
            button.classList.add("today-control-error");
            window.setTimeout(function () {
                button.classList.remove("today-control-error");
            }, 1400);
        } finally {
            button.disabled = false;
        }
    });
}());
