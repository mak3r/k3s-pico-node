# Quick Start Guide

Get your Pico W talking to k3s in under 10 minutes.

## Prerequisites Checklist

- [ ] Raspberry Pi Pico W (with WiFi)
- [ ] k3s server running on local network
- [ ] Pico SDK installed
- [ ] USB cable for Pico
- [ ] 2.4GHz WiFi network (WPA2/WPA3)

## Step 1: Set Up nginx Proxy (5 minutes)

On your **k3s server**:

```bash
# Install nginx
sudo apt install -y nginx  # Ubuntu/Debian
# or: sudo zypper install -y nginx  # openSUSE

# Create proxy config
sudo tee /etc/nginx/sites-available/k3s-proxy > /dev/null <<'EOF'
server {
    listen 6080;
    server_name _;

    allow 192.168.0.0/16;
    allow 10.0.0.0/8;
    deny all;

    location / {
        proxy_pass https://127.0.0.1:6443;
        proxy_ssl_certificate /var/lib/rancher/k3s/server/tls/client-admin.crt;
        proxy_ssl_certificate_key /var/lib/rancher/k3s/server/tls/client-admin.key;
        proxy_ssl_trusted_certificate /var/lib/rancher/k3s/server/tls/server-ca.crt;
        proxy_ssl_verify on;

        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_buffering off;
    }
}
EOF

# Enable and restart
sudo ln -s /etc/nginx/sites-available/k3s-proxy /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx

# Open firewall
sudo firewall-cmd --zone=internal --add-port=6080/tcp --permanent
sudo firewall-cmd --reload
# or for ufw: sudo ufw allow from 192.168.0.0/16 to any port 6080

# Test it works
curl http://localhost:6080/version
```

Expected: JSON with k3s version info.

**Troubleshooting**: See [NGINX-PROXY-SETUP.md](NGINX-PROXY-SETUP.md#troubleshooting)

## Step 2: Configure Pico (1 minute)

On your **development machine**:

```bash
cd k3s-pico-node

# Copy config template
cp include/config_local.h.template include/config_local.h

# Edit with your credentials
nano include/config_local.h
```

Update these values:
```c
#define WIFI_SSID         "your-wifi-name"
#define WIFI_PASSWORD     "your-wifi-password"
#define K3S_SERVER_IP     "192.168.x.x"  // Your k3s server IP
#define K3S_SERVER_PORT   6080            // nginx proxy port
```

## Step 3: Build Firmware (2 minutes)

```bash
# Set SDK path
export PICO_SDK_PATH=/path/to/pico-sdk

# Build
mkdir -p build && cd build
cmake ..
make -j4
```

Output: `k3s_pico_node.uf2`

## Step 4: Flash Pico (1 minute)

```bash
# Hold BOOTSEL button on Pico while plugging in USB
# Pico appears as USB drive "RPI-RP2"

# Copy firmware
cp k3s_pico_node.uf2 /media/$USER/RPI-RP2/

# Pico reboots automatically
```

## Step 5: Monitor (1 minute)

Connect serial monitor:

```bash
screen /dev/ttyACM0 115200
# or: minicom -D /dev/ttyACM0 -b 115200
```

Expected output:
```
Pico K3s Node Starting...
Connecting to WiFi 'your-wifi-name'...
WiFi connected! IP: 192.168.x.x
Registering with k3s...
Node registered successfully!
Status reporting every 10s...
```

## Step 6: Verify in k3s

On your **k3s server**:

```bash
# Check node appears
kubectl get nodes

# Should show:
# NAME          STATUS   ROLES    AGE   VERSION
# pico-node-1   Ready    <none>   1m    v1.28.0

# View details
kubectl describe node pico-node-1
```

## Step 7: Test ConfigMap (optional)

```bash
# Create ConfigMap to update Pico memory
kubectl create configmap pico-config \
  --from-literal=memory_values="0=0x42,1=0x43,10=0xFF"

# Wait ~30s for Pico to poll and apply

# Update ConfigMap
kubectl patch configmap pico-config \
  --type merge \
  -p '{"data":{"memory_values":"0=0xAA,1=0xBB,100=0xCC"}}'

# Check Pico serial output for "ConfigMap updated" message
```

## Common Issues

### WiFi Won't Connect

- Check SSID and password in `config_local.h`
- Verify 2.4GHz network (Pico W doesn't support 5GHz)
- Ensure WPA2/WPA3 security (not WEP or open)

### Node Registration Fails

- Test proxy: `curl http://192.168.x.x:6080/version`
- Check firewall allows port 6080 from Pico's IP
- Verify k3s server IP is correct in `config_local.h`
- Check serial output for error messages

### Node Shows as NotReady

- Status reporting may take 10-20 seconds after registration
- Check serial output for "Status updated" messages
- Verify Pico can reach kubelet: `curl http://pico-ip:10250/healthz`

## Next Steps

- Read [ARCHITECTURE.md](ARCHITECTURE.md) to understand system design
- Read [TLS-PROXY-RATIONALE.md](TLS-PROXY-RATIONALE.md) for security details
- Test ConfigMap updates
- Run 24-hour endurance test

## Getting Help

- Check [NGINX-PROXY-SETUP.md](NGINX-PROXY-SETUP.md) for detailed troubleshooting
- Review serial output for error messages
- Check nginx logs: `sudo tail -f /var/log/nginx/k3s-proxy-error.log`
- Check k3s logs: `sudo journalctl -u k3s -f`

## Documentation

- [Architecture](ARCHITECTURE.md) - System design
- [nginx Proxy Setup](NGINX-PROXY-SETUP.md) - Detailed proxy setup
- [TLS Proxy Rationale](TLS-PROXY-RATIONALE.md) - Security analysis
- [Main README](../README.md) - Full project documentation

---

**Time Investment**: ~10 minutes setup + build time
**Difficulty**: Beginner-friendly
**Prerequisites**: Basic Linux command line, k3s installed
