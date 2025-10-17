document.addEventListener("DOMContentLoaded", async () => {
    const mqtt_form = document.getElementById("mqtt-form");
    const mqtt_status = document.getElementById("mqtt-status");
    const ble_form = document.getElementById("ble-form");
    const ble_status = document.getElementById("ble-status");
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
    mqtt_form.addEventListener("submit", async (e) => {
        e.preventDefault();
        const payload = {
            broker: mqtt_form.broker.value,
            prefix: mqtt_form.prefix.value,
            user: mqtt_form.user.value,
            pass: mqtt_form.pass.value,
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
    ble_form.addEventListener("submit", async (e) => {
        e.preventDefault();
        const payload = {
            device_name: ble_form.device_name.value,
        };
        const res = await fetch("/ble_submit", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });
        if (res.ok) {
            const json = await res.json();
            ble_status.textContent = json.message || "New configuration applied";
            ble_status.style.color = "green";
        } else {
            ble_status.textContent = "Failed to save configuration";
            ble_status.style.color = "red";
        }
    });
});
