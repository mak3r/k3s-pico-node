# nginx TLS Proxy Setup Guide

This guide walks through setting up an nginx reverse proxy on your k3s server to terminate TLS for Pico clients.

## Why Use a Proxy?

The Raspberry Pi Pico W lacks the resources and security features needed for robust TLS:
- No secure storage for certificates/keys
- Limited RAM (264KB total)
- No hardware crypto acceleration
- mbedtls/Go TLS compatibility issues

By using server-side TLS termination, we:
- Eliminate TLS overhead on the Pico
- Simplify debugging (plain HTTP from Pico)
- Maintain k3s API compatibility
- Keep TLS where it's most effective (server-side)

For detailed security rationale, see [TLS-PROXY-RATIONALE.md](TLS-PROXY-RATIONALE.md).

## Architecture

```
┌─────────────────┐         ┌──────────────────────────────────┐
│                 │  HTTP   │  k3s Server (192.168.x.x)        │
│  Pico W         ├────────►│  ┌─────────────────┐             │
│  192.168.x.x    │ :6080   │  │ nginx proxy     │  HTTPS      │
│                 │         │  │ (TLS term)      ├────────────► │
└─────────────────┘         │  └─────────────────┘   :6443     │
                            │           │                       │
                            │           v                       │
                            │  ┌─────────────────┐             │
                            │  │ k3s API server  │             │
                            │  │ localhost:6443  │             │
                            │  └─────────────────┘             │
                            └──────────────────────────────────┘
```

## Prerequisites

- k3s installed and running
- Root or sudo access to k3s server
- k3s server accessible on local network
- Firewall management tool (firewalld or ufw)

## Step 1: Install nginx

### On Ubuntu/Debian

```bash
sudo apt update
sudo apt install -y nginx
```

### On openSUSE

```bash
sudo zypper refresh
sudo zypper install -y nginx
```

### On RHEL/Fedora

```bash
sudo dnf install -y nginx
```

Verify installation:

```bash
nginx -v
# Should show: nginx version: nginx/1.x.x
```

## Step 2: Create Proxy Configuration

Create the k3s proxy configuration file:

```bash
sudo tee /etc/nginx/sites-available/k3s-proxy > /dev/null <<'EOF'
server {
    # Listen on port 6080 for HTTP requests from Picos
    listen 6080;
    server_name _;

    # SECURITY: Only allow local network access
    # Adjust these subnets to match your network
    allow 192.168.0.0/16;  # Private Class C
    allow 10.0.0.0/8;      # Private Class A
    allow 172.16.0.0/12;   # Private Class B
    deny all;              # Deny everything else

    # Logging
    access_log /var/log/nginx/k3s-proxy-access.log;
    error_log /var/log/nginx/k3s-proxy-error.log;

    location / {
        # Forward to k3s API on localhost
        proxy_pass https://127.0.0.1:6443;

        # Use k3s admin certificate for authentication
        # These files are created by k3s installation
        proxy_ssl_certificate /var/lib/rancher/k3s/server/tls/client-admin.crt;
        proxy_ssl_certificate_key /var/lib/rancher/k3s/server/tls/client-admin.key;
        proxy_ssl_trusted_certificate /var/lib/rancher/k3s/server/tls/server-ca.crt;
        proxy_ssl_verify on;
        proxy_ssl_verify_depth 2;

        # Standard proxy headers
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_set_header X-Forwarded-Host $host;
        proxy_set_header X-Forwarded-Port $server_port;

        # Kubernetes API requires HTTP/1.1 for WebSocket upgrades (Watch API)
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";

        # Disable buffering for streaming responses (Watch API)
        proxy_buffering off;
        proxy_request_buffering off;

        # Timeouts (Kubernetes operations can be long-running)
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
    }
}
EOF
```

### For openSUSE (sites-available doesn't exist by default)

```bash
# Create sites-available and sites-enabled directories
sudo mkdir -p /etc/nginx/sites-{available,enabled}

# Add include directive to main nginx.conf if not present
if ! grep -q "sites-enabled" /etc/nginx/nginx.conf; then
    sudo sed -i '/http {/a \    include /etc/nginx/sites-enabled/*;' /etc/nginx/nginx.conf
fi
```

## Step 3: Enable the Site

### Ubuntu/Debian (uses sites-enabled by default)

```bash
sudo ln -sf /etc/nginx/sites-available/k3s-proxy /etc/nginx/sites-enabled/
```

### openSUSE (if created sites-enabled above)

```bash
sudo ln -sf /etc/nginx/sites-available/k3s-proxy /etc/nginx/sites-enabled/
```

### Alternative (direct include)

If your nginx doesn't use sites-enabled, copy directly:

```bash
sudo cp /etc/nginx/sites-available/k3s-proxy /etc/nginx/conf.d/k3s-proxy.conf
```

## Step 4: Test Configuration

```bash
sudo nginx -t
```

Expected output:
```
nginx: the configuration file /etc/nginx/nginx.conf syntax is ok
nginx: configuration file /etc/nginx/nginx.conf test is successful
```

If you see errors:
- Check file paths exist (k3s certificates)
- Verify syntax of configuration file
- Check for conflicting configurations

## Step 5: Start/Restart nginx

```bash
# Enable nginx to start on boot
sudo systemctl enable nginx

# Restart nginx to apply configuration
sudo systemctl restart nginx

# Check status
sudo systemctl status nginx
```

Should show: `active (running)`

## Step 6: Configure Firewall

**IMPORTANT**: Only allow local network traffic to port 6080. Never expose this port to the internet.

### For firewalld (openSUSE, RHEL, Fedora)

```bash
# Add port 6080 to internal zone (trusted local network)
sudo firewall-cmd --zone=internal --add-port=6080/tcp --permanent

# If your local network interface is in the public zone, add rule there instead
# First check your zones:
sudo firewall-cmd --get-active-zones

# If your interface is in public zone, add source restriction:
sudo firewall-cmd --zone=public --add-rich-rule='rule family="ipv4" source address="192.168.0.0/16" port protocol="tcp" port="6080" accept' --permanent

# Reload firewall
sudo firewall-cmd --reload

# Verify
sudo firewall-cmd --list-all
```

### For ufw (Ubuntu, Debian)

```bash
# Allow port 6080 only from local network
sudo ufw allow from 192.168.0.0/16 to any port 6080 proto tcp
sudo ufw allow from 10.0.0.0/8 to any port 6080 proto tcp

# Reload firewall
sudo ufw reload

# Verify
sudo ufw status
```

### For iptables (manual)

```bash
# Allow port 6080 from local networks only
sudo iptables -A INPUT -p tcp --dport 6080 -s 192.168.0.0/16 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 6080 -s 10.0.0.0/8 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 6080 -j DROP

# Make persistent (method varies by distribution)
# Ubuntu/Debian:
sudo apt install iptables-persistent
sudo netfilter-persistent save

# RHEL/CentOS:
sudo service iptables save
```

## Step 7: Test the Proxy

### Test from k3s Server (localhost)

```bash
curl -v http://localhost:6080/version
```

Expected output:
```json
{
  "major": "1",
  "minor": "28",
  "gitVersion": "v1.28.x+k3s1",
  ...
}
```

### Test from Pico Network

From a machine on the same network as the Pico (replace `192.168.x.x` with your k3s server IP):

```bash
curl -v http://192.168.x.x:6080/version
```

Should show the same JSON response.

### Test Node List

```bash
curl http://localhost:6080/api/v1/nodes
```

Should return JSON with list of nodes (may be empty initially).

## Troubleshooting

### Error: Connection Refused

**Problem**: nginx not running or not listening on port 6080

**Solution**:
```bash
sudo systemctl status nginx
sudo ss -tlnp | grep 6080
sudo journalctl -u nginx -f
```

### Error: 502 Bad Gateway

**Problem**: nginx can't connect to k3s API on localhost:6443

**Solutions**:
1. Check k3s is running: `sudo systemctl status k3s`
2. Check k3s API is listening: `sudo ss -tlnp | grep 6443`
3. Check nginx error log: `sudo tail -f /var/log/nginx/k3s-proxy-error.log`

### Error: 403 Forbidden

**Problem**: Request coming from IP not in allowed list

**Solution**:
- Check client IP matches allow rules in nginx config
- Update allow statements to include your network subnet
- Restart nginx after changes

### Error: SSL Certificate Problem

**Problem**: nginx can't verify k3s API certificate or use client certificate

**Solutions**:
1. Verify certificate files exist:
   ```bash
   ls -l /var/lib/rancher/k3s/server/tls/client-admin.*
   ls -l /var/lib/rancher/k3s/server/tls/server-ca.crt
   ```

2. Check nginx has permission to read certificates:
   ```bash
   sudo -u www-data cat /var/lib/rancher/k3s/server/tls/client-admin.crt
   ```

3. If permission denied, add nginx user to k3s group:
   ```bash
   sudo usermod -a -G k3s www-data
   sudo systemctl restart nginx
   ```

### Error: Can't Access from Pico Network

**Problem**: Firewall blocking connections

**Solutions**:
1. Check firewall rules: `sudo firewall-cmd --list-all` or `sudo ufw status`
2. Verify Pico IP is in allowed subnet
3. Temporarily disable firewall to test: `sudo systemctl stop firewalld` (don't forget to re-enable!)
4. Check nginx access log for rejected requests: `sudo tail -f /var/log/nginx/k3s-proxy-access.log`

### Enable Debug Logging

For more verbose nginx logs, add to server block:

```nginx
error_log /var/log/nginx/k3s-proxy-error.log debug;
```

Then restart nginx and check logs:

```bash
sudo systemctl restart nginx
sudo tail -f /var/log/nginx/k3s-proxy-error.log
```

## Security Considerations

### What This Protects

1. **k3s API remains secure**: TLS between nginx and k3s API
2. **Certificate management centralized**: Only server handles certificates
3. **Network boundary protection**: Firewall restricts access to local network only

### What This Doesn't Protect

1. **Pico-to-proxy traffic is HTTP**: Unencrypted on local network (acceptable on trusted WiFi)
2. **Shared identity**: All Picos use the same identity (nginx's admin cert)
3. **Physical security**: Anyone with physical access to Pico can extract WiFi credentials

### Recommendations

1. **Use WPA2/WPA3 WiFi**: Provides encryption at the WiFi layer
2. **Isolate Pico network**: Consider separate VLAN for IoT devices
3. **Never expose port 6080 to internet**: Firewall must restrict to local network only
4. **Monitor access logs**: Watch for unexpected access patterns
5. **Rotate admin certificate**: Follow k3s best practices for certificate rotation

## Performance

Expected latency breakdown:
- Pico → nginx: 5-10ms (local network)
- nginx → k3s: 1-5ms (localhost)
- Total overhead: ~10ms added vs direct connection

The proxy adds minimal overhead while providing significant simplification for embedded clients.

## Alternative: Direct TLS

If you need direct TLS from Pico (e.g., for untrusted network):

1. Switch to microcontroller with secure enclave (ESP32-S3, STM32H7)
2. Implement per-device certificate provisioning
3. Use hardware crypto acceleration
4. Accept increased complexity and memory usage

For most embedded IoT projects on trusted local networks, the proxy approach is recommended.

## References

- [TLS Proxy Security Rationale](TLS-PROXY-RATIONALE.md)
- [Architecture Documentation](ARCHITECTURE.md)
- [nginx Proxy Module Documentation](http://nginx.org/en/docs/http/ngx_http_proxy_module.html)
- [k3s Architecture](https://docs.k3s.io/architecture)
