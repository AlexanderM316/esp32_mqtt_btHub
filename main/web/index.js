document.addEventListener("DOMContentLoaded", async () => {
    const form = document.getElementById("mqtt-form");
    const status = document.getElementById("status");
    try {
        const res = await fetch("/index.json");
        if (res.ok) {
            const data = await res.json();
            document.getElementById("broker").value = data.broker || "";
            document.getElementById("prefix").value = data.prefix || "";
            document.getElementById("user").value = data.user || "";
            document.getElementById("pass").value = data.pass || "";
        }
    } catch (err) {
        console.log("No existing config found");
    }
    form.addEventListener("submit", async (e) => {
        e.preventDefault();
        const payload = {
            broker: form.broker.value,
            prefix: form.prefix.value,
            user: form.user.value,
            pass: form.pass.value,
        };
        const res = await fetch("/mqtt_submit", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });
        if (res.ok) {
            const json = await res.json();
            status.textContent = json.message || "New configuration applied";
            status.style.color = "green";
        } else {
            status.textContent = "Failed to save configuration";
            status.style.color = "red";
        }
    });
});
