document.addEventListener("DOMContentLoaded", () => {
    const form = document.getElementById("login-form");
    const status = document.getElementById("login-status");

    form.addEventListener("submit", async (e) => {
        e.preventDefault();
        status.textContent = "Logging in...";
        status.style.color = "black";

        const user = document.getElementById("user").value.trim();
        const pass = document.getElementById("pass").value.trim();

        try {
            const res = await fetch("/login", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ user, pass })
            });

            if (res.ok) {
                // Successful login → redirect to main page
                window.location.href = "/index.html";
            } else {
                const text = await res.text();
                status.textContent = text || "Invalid credentials";
                status.style.color = "red";
            }
        } catch (err) {
            status.textContent = "Error connecting to device";
            status.style.color = "red";
        }
    });
});
