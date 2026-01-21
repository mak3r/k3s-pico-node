# Cluster Admin Guide: Integrating Embedded Nodes

## Overview

This guide helps Kubernetes cluster administrators understand and manage embedded microcontroller nodes (like Raspberry Pi Pico) that run embedded firmware instead of standard OCI containers.

## What are Embedded Nodes?

Embedded nodes are microcontroller-based Kubernetes nodes that:

- ✅ Run embedded firmware (compiled directly for the hardware)
- ✅ Integrate with Kubernetes API (registration, status reporting)
- ✅ Report hardware resources (GPIO pins, sensors, actuators)
- ✅ Support pod scheduling with resource constraints
- ❌ Cannot pull arbitrary OCI images from registries
- ❌ Cannot run standard containers (no Docker/containerd)
- ❌ Use flash storage as "ECI registry" for embedded container images (ECI)

**Think of them as:** Specialized worker nodes for edge/IoT workloads with hardware-specific capabilities.

## Node Characteristics

### Hardware Profile (Example: Raspberry Pi Pico W)

```
Platform:     Raspberry Pi Pico W
CPU:          Dual-core ARM Cortex-M0+ @ 133MHz
RAM:          264 KB
Storage:      2 GB flash (QSPI)
Network:      2.4GHz WiFi (802.11n)
Peripherals:  29 GPIO, 1 LED, temp sensor, ADC, PWM
```

### Kubernetes Integration

```yaml
# What the node reports to K8s
status:
  capacity:
    cpu: "2"                    # 2 cores
    memory: "264Ki"             # 264 KB RAM
    ephemeral-storage: "2Gi"    # 2 GB flash
    pods: "10"                  # Max 10 pods
    pico.io/gpio-pin: "29"      # Hardware resources
    pico.io/onboard-led: "1"
    pico.io/temp-sensor: "1"

  allocatable:
    cpu: "1800m"                # 90% available
    memory: "200Ki"             # ~76% available
    ephemeral-storage: "1900Mi" # System overhead
    pods: "10"
    pico.io/gpio-pin: "29"
    pico.io/onboard-led: "1"
    pico.io/temp-sensor: "1"
```

## Node Lifecycle

### 1. Boot and Initialization

```
Pico boots → WiFi connect → Hardware discovery → K8s registration
```

**What happens:**
- Discovers available hardware (GPIO, sensors, LEDs)
- Loads embedded container images (ECI) from flash registry
- Connects to K8s API server
- Creates node object with taints and labels

### 2. Node Registration

The node self-registers with these characteristics:

```yaml
apiVersion: v1
kind: Node
metadata:
  name: pico-node-1
  labels:
    kubernetes.io/arch: arm
    kubernetes.io/os: baremetal
    pico.io/board: pico-w
    pico.io/workload-type: embedded
    pico.io/image-registry: flash-only
    pico.io/oci-compatible: "false"
    hardware.pico.io/has-led: "true"
    hardware.pico.io/has-temp-sensor: "true"
  annotations:
    pico.io/available-ecis: '["pico/gpio-controller:v1","pico/temp-sensor:v1"]'
spec:
  taints:
  - key: pico.io/embedded-workload
    value: "true"
    effect: NoSchedule
  - key: pico.io/oci-incompatible
    value: "true"
    effect: NoExecute
status:
  conditions:
  - type: Ready
    status: "True"
  - type: EmbeddedWorkload
    status: "True"
    reason: FlashOnlyRegistry
    message: "Node runs embedded firmware from flash storage"
  - type: OCICompatible
    status: "False"
    reason: NoContainerRuntime
    message: "Node cannot pull/run standard OCI images"
```

### 3. Status Reporting

Every 10 seconds:
- Reports node conditions (Ready, MemoryPressure, etc.)
- Updates resource allocation status
- Reports pod statuses

## Scheduling Behavior

### Taints Prevent Accidental Scheduling

Embedded nodes automatically taint themselves:

```yaml
taints:
- key: pico.io/embedded-workload
  value: "true"
  effect: NoSchedule        # Prevents standard pods
- key: pico.io/oci-incompatible
  value: "true"
  effect: NoExecute         # Evicts incompatible pods
```

**Result:**
- ✅ Standard pods **will not** schedule to embedded nodes
- ✅ DaemonSets **will skip** embedded nodes (by default)
- ✅ Only pods with tolerations can schedule
- ✅ Prevents wasted time trying to pull unavailable images

### How Pods Schedule to Embedded Nodes

**Requirements for pod scheduling:**

1. **Tolerate the taints**
2. **Select embedded nodes** (via nodeSelector or affinity)
3. **Request embedded container image (ECI)** (available in flash)
4. **Request hardware resources** (if needed)

**Example pod manifest:**

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: gpio-controller
spec:
  # 1. Tolerate taints
  tolerations:
  - key: pico.io/embedded-workload
    operator: Equal
    value: "true"
    effect: NoSchedule
  - key: pico.io/oci-incompatible
    operator: Exists
    effect: NoExecute

  # 2. Select embedded node
  nodeSelector:
    pico.io/workload-type: embedded

  # 3. Use embedded container image / ECI (from flash)
  containers:
  - name: gpio
    image: pico/gpio-controller:v1

    # 4. Request hardware resources
    resources:
      limits:
        memory: "64Ki"
        cpu: "500m"
        pico.io/onboard-led: 1
        pico.io/gpio-pin: 2
```

### DaemonSets and Embedded Nodes

**Default behavior:** DaemonSets skip embedded nodes.

**To include embedded nodes in a DaemonSet:**

```yaml
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: node-exporter
spec:
  template:
    spec:
      tolerations:
      - key: pico.io/embedded-workload
        operator: Exists  # Tolerate all embedded nodes
      containers:
      - name: exporter
        image: pico/node-exporter:v1  # Must be available ECI in flash!
```

**To exclude embedded nodes (default):**

Don't add tolerations - the taint will prevent scheduling.

## Admin Operations

### List All Embedded Nodes

```bash
kubectl get nodes -l pico.io/workload-type=embedded

# Example output:
# NAME           STATUS   ROLES    AGE   VERSION
# pico-node-1    Ready    <none>   5d    v1.28.0
# pico-node-2    Ready    <none>   3d    v1.28.0
```

### View Node Details

```bash
kubectl describe node pico-node-1
```

**Look for:**
- Taints (should have pico.io/embedded-workload)
- Labels (pico.io/*, hardware.pico.io/*)
- Capacity/Allocatable (including hardware resources)
- Conditions (EmbeddedWorkload, OCICompatible)
- Allocated Resources

### Check Available Embedded Container Images (ECIs)

```bash
# JSON format
kubectl get node pico-node-1 \
  -o jsonpath='{.metadata.annotations.pico\.io/available-ecis}'

# Pretty print
kubectl get node pico-node-1 \
  -o jsonpath='{.metadata.annotations.pico\.io/available-ecis}' | jq .
```

**Example output:**
```json
[
  "pico/gpio-controller:v1",
  "pico/temp-sensor:v1",
  "pico/led-matrix:v1"
]
```

### View Hardware Resources

```bash
# All hardware resources
kubectl describe node pico-node-1 | grep pico.io/

# Example output:
#  pico.io/gpio-pin:       29
#  pico.io/onboard-led:    1
#  pico.io/temp-sensor:    1
```

### Check Resource Allocation

```bash
kubectl describe node pico-node-1 | grep -A 10 "Allocated resources"

# Example output:
# Allocated resources:
#   (Total limits may be over 100 percent, i.e., overcommitted.)
#   Resource             Requests    Limits
#   --------             --------    ------
#   cpu                  500m (27%)  500m (27%)
#   memory               64Ki (32%)  64Ki (32%)
#   pico.io/gpio-pin     2 (6%)      2 (6%)
#   pico.io/onboard-led  1 (100%)    1 (100%)
```

### View Pods on Embedded Node

```bash
kubectl get pods --field-selector spec.nodeName=pico-node-1

# Or with more details
kubectl get pods -o wide | grep pico-node-1
```

### Deploy Workload to Embedded Node

```bash
# Using example manifest
kubectl apply -f examples/gpio-controller-pod.yaml

# Verify scheduling
kubectl get pod gpio-controller -o wide

# Check logs
kubectl logs gpio-controller
```

## Monitoring and Observability

### What's Normal (Not Errors!)

These are **expected behaviors** on embedded nodes:

✅ **DaemonSets showing 0 pods** - Normal if DaemonSet doesn't tolerate embedded taint
✅ **Low pod count** - Embedded nodes typically run 1-3 specialized pods
✅ **Custom node conditions** - EmbeddedWorkload=True, OCICompatible=False
✅ **Different resource types** - Hardware resources instead of just CPU/memory
✅ **ImagePullBackOff on wrong ECI** - Expected if pod requests unavailable embedded container image

### What to Monitor

**Node Health:**
```bash
# Check Ready condition
kubectl get node pico-node-1 -o jsonpath='{.status.conditions[?(@.type=="Ready")].status}'

# Should return: True
```

**Resource Availability:**
```bash
# Check allocatable resources
kubectl describe node pico-node-1 | grep -A 5 "Allocatable:"
```

**Pod Failures:**
```bash
# Check for failed pods
kubectl get pods --field-selector spec.nodeName=pico-node-1,status.phase=Failed
```

### Common Issues and Troubleshooting

#### Pod Stuck in Pending

**Symptom:**
```bash
kubectl get pod my-pod
# NAME     READY   STATUS    RESTARTS   AGE
# my-pod   0/1     Pending   0          2m
```

**Causes and Solutions:**

1. **Missing toleration**
   ```bash
   kubectl describe pod my-pod | grep -A 5 "Tolerations:"
   # Should show pico.io/embedded-workload toleration
   ```

2. **No matching nodes**
   ```bash
   kubectl describe pod my-pod | grep "Events:"
   # Look for: "0/5 nodes are available: 1 node(s) had taints that the pod didn't tolerate"
   ```

3. **Insufficient resources**
   ```bash
   kubectl describe node pico-node-1 | grep "Allocated resources"
   # Check if requested resources are available
   ```

#### Pod Fails with ImageNotFound

**Symptom:**
```bash
kubectl describe pod my-pod
# Events:
#   Warning  Failed  ECI 'pico/nonexistent:v1' not available in flash registry
```

**Solution:**

Check available ECIs:
```bash
kubectl get node pico-node-1 \
  -o jsonpath='{.metadata.annotations.pico\.io/available-ecis}' | jq .
```

Compile and flash the required ECI (embedded container image), or update pod to use available ECI.

#### Hardware Resource Exhausted

**Symptom:**
```bash
kubectl describe pod my-pod
# Events:
#   Warning  FailedScheduling  insufficient pico.io/onboard-led
```

**Solution:**

Check what's using the LED:
```bash
kubectl describe node pico-node-1 | grep -A 10 "Allocated resources"
# Shows which pods allocated which hardware
```

Delete or reschedule competing pods.

#### Node Not Reporting Ready

**Symptom:**
```bash
kubectl get node pico-node-1
# NAME          STATUS      ROLES    AGE
# pico-node-1   NotReady    <none>   5m
```

**Check:**
1. Network connectivity (WiFi connection)
2. K8s API server reachable
3. Node logs (serial console on Pico)

**Debug from Pico serial console:**
```
# Expected output:
WiFi connected! IP address: 192.168.86.249
Node registered successfully
Status report sent
```

## Best Practices

### 1. Namespace Organization

Group embedded workloads in dedicated namespace:

```bash
kubectl create namespace iot-edge
kubectl label namespace iot-edge workload-type=embedded
```

### 2. Resource Quotas

Set quotas for embedded resources:

```yaml
apiVersion: v1
kind: ResourceQuota
metadata:
  name: embedded-quota
  namespace: iot-edge
spec:
  hard:
    pico.io/gpio-pin: "100"     # Max GPIO pins across all pods
    pico.io/onboard-led: "10"   # Max LEDs
```

### 3. Pod Disruption Budgets

Protect critical embedded workloads:

```yaml
apiVersion: policy/v1
kind: PodDisruptionBudget
metadata:
  name: gpio-controller-pdb
spec:
  minAvailable: 1
  selector:
    matchLabels:
      app: gpio-controller
```

### 4. Monitoring Integration

**Prometheus scraping:**

Embedded nodes expose `/metrics` endpoint on kubelet port (10250):

```yaml
# prometheus.yml
scrape_configs:
- job_name: 'pico-nodes'
  kubernetes_sd_configs:
  - role: node
  relabel_configs:
  - source_labels: [__meta_kubernetes_node_label_pico_io_workload_type]
    regex: embedded
    action: keep
  metrics_path: /metrics
  scheme: https
  tls_config:
    insecure_skip_verify: true
```

**Grafana dashboard:**

Monitor:
- Node uptime
- Resource utilization (CPU, memory, flash)
- Hardware allocation (GPIO, sensors)
- Pod count per node
- Network connectivity

### 5. Documentation

Document your embedded container images (ECIs):

```bash
# Create ConfigMap with ECI documentation
kubectl create configmap embedded-eci-docs \
  --from-literal=gpio-controller='Controls GPIO pins via K8s' \
  --from-literal=temp-sensor='Reads temperature, exposes as metrics' \
  -n kube-system
```

## Security Considerations

### Network Policies

Embedded nodes may need different network policies:

```yaml
apiVersion: networking.k8s.io/v1
kind: NetworkPolicy
metadata:
  name: embedded-nodes-policy
spec:
  podSelector:
    matchLabels:
      pico.io/workload-type: embedded
  policyTypes:
  - Ingress
  - Egress
  ingress:
  - from:
    - namespaceSelector:
        matchLabels:
          monitoring: enabled
  egress:
  - to:
    - namespaceSelector: {}
    ports:
    - protocol: TCP
      port: 6443  # K8s API server
```

### RBAC

Embedded nodes use client certificates for authentication.

**View node credentials:**
```bash
kubectl get csr | grep pico-node
```

**Node permissions (minimal):**
- Create/update own node object
- Report pod statuses
- Read ConfigMaps (if ConfigMap watcher enabled)

### TLS Certificates

Certificates stored in flash:
- Server CA (verify K8s API server)
- Client certificate (authenticate node)
- Client key (private key)

**Certificate rotation:**

Embedded nodes support certificate rotation. Certificates typically valid for 1 year.

## Capacity Planning

### Nodes per Cluster

**Small cluster (< 10 embedded nodes):**
- No special configuration needed
- Standard K8s setup works fine

**Medium cluster (10-50 embedded nodes):**
- Consider dedicated nodePool or machine set
- Use node affinity to group on fewer schedulers
- Monitor API server load

**Large cluster (50+ embedded nodes):**
- Use federation or edge clusters
- Consider edge-specific control plane (K3s, MicroK8s)
- Batch status updates from nodes

### Resource Distribution

**Per node capacity (Pico W example):**
- CPU: ~1800m allocatable
- Memory: ~200Ki allocatable
- Pods: 10 max (practical: 3-5)
- Hardware: Varies by board

**Planning:**
- 1 embedded node ≈ 1-3 specialized workloads
- Hardware resources are typically exclusive (1 user)
- Over-subscription not recommended for embedded nodes

## Example Workflows

### Deploy GPIO Controller Fleet

```bash
# Create namespace
kubectl create namespace iot-devices

# Deploy to all Pico nodes
kubectl apply -f - <<EOF
apiVersion: apps/v1
kind: DaemonSet
metadata:
  name: gpio-controller
  namespace: iot-devices
spec:
  selector:
    matchLabels:
      app: gpio-controller
  template:
    metadata:
      labels:
        app: gpio-controller
    spec:
      tolerations:
      - key: pico.io/embedded-workload
        operator: Exists
      nodeSelector:
        pico.io/workload-type: embedded
        hardware.pico.io/has-led: "true"
      containers:
      - name: gpio
        image: pico/gpio-controller:v1
        resources:
          limits:
            pico.io/onboard-led: 1
EOF

# Verify deployment
kubectl get ds -n iot-devices
kubectl get pods -n iot-devices -o wide
```

### Update Firmware (Rolling Update)

```bash
# Drain node (graceful pod eviction)
kubectl drain pico-node-1 --ignore-daemonsets --delete-emptydir-data

# Flash new firmware to Pico
# (Physical operation - hold BOOTSEL, copy .uf2 file)

# Pico reboots, re-registers with K8s

# Uncordon node
kubectl uncordon pico-node-1

# Verify
kubectl get node pico-node-1
kubectl get pods --field-selector spec.nodeName=pico-node-1
```

## Advanced Topics

### Custom Resource Definitions

Define your own embedded workload types:

```yaml
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: gpiocontrollers.pico.io
spec:
  group: pico.io
  names:
    kind: GPIOController
    plural: gpiocontrollers
  scope: Namespaced
  versions:
  - name: v1
    served: true
    storage: true
    schema:
      openAPIV3Schema:
        type: object
        properties:
          spec:
            type: object
            properties:
              pinNumber:
                type: integer
              mode:
                type: string
                enum: [input, output, pwm]
```

### Operator Pattern

Build an operator that manages embedded workloads across multiple nodes.

See Issue #7 for the Embedded Node Controller.

## References

- [ARCHITECTURE_PLAN.md](ARCHITECTURE_PLAN.md) - System architecture
- [HARDWARE_RESOURCE_MANAGEMENT.md](HARDWARE_RESOURCE_MANAGEMENT.md) - Hardware resources
- [PORTING_GUIDE.md](PORTING_GUIDE.md) - Porting to new boards
- [Kubernetes Taints and Tolerations](https://kubernetes.io/docs/concepts/scheduling-eviction/taint-and-toleration/)
- [Extended Resources](https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/#extended-resources)

## Support

For issues specific to embedded nodes:
- GitHub Issues: https://github.com/mak3r/k3s-pico-node/issues
- Tag with: `embedded-nodes`, `scheduling`, or `cluster-admin`

## Summary

**Key Takeaways:**
- ✅ Embedded nodes are specialized K8s nodes for edge/IoT workloads
- ✅ Taints prevent accidental scheduling of standard workloads
- ✅ Pods must tolerate taints and request embedded container images (ECIs)
- ✅ DaemonSets skip embedded nodes by default (by design!)
- ✅ Hardware resources (GPIO, sensors) are Kubernetes-schedulable
- ✅ Monitoring and management use standard K8s tools

**Remember:** Embedded nodes are not broken standard nodes - they're purpose-built for edge workloads!
