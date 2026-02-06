const express = require('express');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 5050;

// Serve static files from current directory
app.use(express.static(__dirname));

// Default route - serve WiFi controller
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'controller_wifi.html'));
});

// Legacy BLE controller route
app.get('/ble', (req, res) => {
    res.sendFile(path.join(__dirname, 'controller.html'));
});

// Get local IP addresses for easy access
function getLocalIPs() {
    const os = require('os');
    const interfaces = os.networkInterfaces();
    const ips = [];

    for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                ips.push(iface.address);
            }
        }
    }
    return ips;
}

app.listen(PORT, '0.0.0.0', () => {
    console.log('\n========================================');
    console.log('   RoseOS Web Controller Server');
    console.log('========================================\n');
    console.log(`Local:    http://localhost:${PORT}`);

    const ips = getLocalIPs();
    ips.forEach(ip => {
        console.log(`Network:  http://${ip}:${PORT}`);
    });

    console.log('\n→ Open the Network URL on your phone!');
    console.log('→ Make sure phone is on same WiFi as this PC');
    console.log('\nPress Ctrl+C to stop\n');
});
