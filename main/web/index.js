document.addEventListener("DOMContentLoaded", async () => {
    const tabButtons = document.querySelectorAll('.tab-button');
    const tabContents = document.querySelectorAll('.tab-content');
    tabButtons.forEach(button => {
        button.addEventListener('click', () => {
            const tabId = button.getAttribute('data-tab');
            tabButtons.forEach(btn => btn.classList.remove('active'));
            tabContents.forEach(content => content.classList.remove('active'));
            button.classList.add('active');
            document.getElementById(tabId).classList.add('active');
        });
    });
    const mqtt_form = document.getElementById("mqtt-form");
    const mqtt_status = document.getElementById("mqtt-status");
    const ble_form = document.getElementById("ble-form");
    const ble_status = document.getElementById("ble-status");
    const login_form = document.getElementById("set-login-form");
    const login_status = document.getElementById("set-login-status");
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
            mqtt_status.textContent = json.message || "New configuration applied";
            mqtt_status.style.color = "green";
        } else {
            mqtt_status.textContent = "Failed to save configuration";
            mqtt_status.style.color = "red";
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
    login_form.addEventListener("submit", async (e) => {
        e.preventDefault();
        const payload = {
            new_user: login_form.new_name.value,
            new_pass: login_form.new_pass.value,
        };
        const res = await fetch("/set_login_submit", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
        });
        if (res.ok) {
            const json = await res.json();
            login_status.textContent = json.message || "New configuration applied";
            login_status.style.color = "green";
        } else {
            login_status.textContent = "Failed to save configuration";
            login_status.style.color = "red";
        }
    });
});
async function updateMetrics() {
  try {
    const res = await fetch('/metrics');
    const data = await res.json();
    const usedPercent = data.used_percent.toFixed(1);
    const free = data.free_heap;
    const total = data.total_heap;
    document.getElementById('uptime').textContent = (data.uptime_ms / 1000).toFixed(1) + 's';
    document.getElementById('min_heap').textContent = data.min_free_heap;
    const bar = document.getElementById('heap-bar');
    const text = document.getElementById('heap-text');
    bar.style.width = `${usedPercent}%`;
    bar.classList.remove('warn', 'crit');
    if (usedPercent > 80) bar.classList.add('crit');
    else if (usedPercent > 60) bar.classList.add('warn');
    text.textContent = `${usedPercent}% used (${free.toLocaleString()} / ${total.toLocaleString()} bytes)`;
  } catch (err) {
    console.error('Failed to fetch metrics:', err);
  }
}
setInterval(updateMetrics, 2000);
updateMetrics();
