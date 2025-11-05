let metricsInterval = null;
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
            if (tabId === 'system') startMetricsPolling();
            else stopMetricsPolling();
        });
    });
    const mqtt_form = document.getElementById("mqtt-form");
    const mqtt_status = document.getElementById("mqtt-status");
    const ble_form = document.getElementById("ble-form");
    const ble_status = document.getElementById("ble-status");
    const deviceNameInput = document.getElementById("device_name");
    const byNameCb = document.getElementById("by_name");
    const uuidInput = document.getElementById("uuid");
    const byUuidCb = document.getElementById("by_uuid");
    const login_form = document.getElementById("set-login-form");
    const login_status = document.getElementById("set-login-status");
    try {
        const res = await fetch("/index.json");
        if (res.ok) {
            const data = await res.json();
            document.getElementById("device_name").value = data.device_name || "";
            document.getElementById("tx_power").value = data.tx_power ?? 0;
            document.getElementById("interval").value = data.interval ?? 0;
            document.getElementById("duration").value = data.duration ?? 0;
            document.getElementById("mtu").value = data.mtu ?? 0;
            document.getElementById("discovered_count").value = data.discovered_count ?? 0;
            document.getElementById("conn_count").value = data.conn_count ?? 0;
            document.getElementById("by_name").checked = data.by_name ?? false;
            document.getElementById("by_uuid").checked = data.by_uuid ?? false;
            document.getElementById("uuid").value = data.uuid ?? "";
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
    deviceNameInput.disabled = !byNameCb.checked;
    byNameCb.addEventListener('change', () => {
        deviceNameInput.disabled = !byNameCb.checked;
    });
    uuidInput.disabled = !byUuidCb.checked;
    byUuidCb.addEventListener('change', () => {
        uuidInput.disabled = !byUuidCb.checked;
    });
    uuidInput.addEventListener('input', () => {
    uuidInput.value = uuidInput.value.toUpperCase();
    });
    ble_form.addEventListener("submit", async (e) => {
        e.preventDefault();
        const payload = {
            device_name: ble_form.device_name.value,
            tx_power: parseInt(ble_form.tx_power.value, 10),
            interval: parseInt(ble_form.interval.value, 10),
            duration: parseInt(ble_form.duration.value, 10),
            mtu: parseInt(ble_form.mtu.value, 10),
            by_name: ble_form.by_name.checked,
            by_uuid: ble_form.by_uuid.checked,
            uuid: parseInt(ble_form.uuid.value, 16)
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
function startMetricsPolling() {
  if (metricsInterval) return;
  updateMetrics(); 
  metricsInterval = setInterval(updateMetrics, 2000);
}

function stopMetricsPolling() {
  if (metricsInterval) {
    clearInterval(metricsInterval);
    metricsInterval = null;
  }
}

async function updateMetrics() {
  try {
    const res = await fetch('/metrics');
    const data = await res.json();
    const usedPercent = data.used_percent.toFixed(1);
    const free = data.free_heap;
    const total = data.total_heap;
    const usedBytes = total - free;
    document.getElementById('uptime').textContent = Math.floor(data.uptime_ms) + 's';
    document.getElementById('min_heap').textContent = data.min_free_heap.toLocaleString();
    document.getElementById('discovered_count').textContent = data.discovered_count.toLocaleString();
    document.getElementById('conn_count').textContent = data.conn_count.toLocaleString();
    document.getElementById('total_heap').textContent = total.toLocaleString();
    const bar = document.getElementById('heap-bar');
    const usedText = document.getElementById('heap-used');
    const freeText = document.getElementById('heap-free');
    bar.style.width = `${usedPercent}%`;
    bar.classList.remove('warn', 'crit');
    if (usedPercent > 85) bar.classList.add('crit');
    else if (usedPercent > 70) bar.classList.add('warn');
    usedText.textContent = `${usedPercent}% (${usedBytes.toLocaleString()} bytes used)`;
    freeText.textContent = `${free.toLocaleString()} bytes free`;
    if (usedPercent > 80) {
      freeText.style.top = '-18px';
      freeText.style.transform = 'none';
      freeText.style.right = '0';
      freeText.style.color = '#000';
    } else {
      freeText.style.top = '50%';
      freeText.style.transform = 'translateY(-50%)';
      freeText.style.color = '#333';
    }
    const table = document.getElementById('devices-table-body');
    table.innerHTML = '';
    for (const dev of data.devices) {
      const row = document.createElement('tr');
      row.innerHTML = `<td>${dev.index}</td><td>${dev.name}</td><td>${dev.mac}</td><td>${dev.uuid}</td><td>${dev.connected}</td><td>${dev.rssi}</td>`;
      table.appendChild(row);
    }
  } catch (err) {
    console.error('Failed to fetch metrics:', err);
  }
}
