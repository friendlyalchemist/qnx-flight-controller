# Setup: building and running on a QNX VM

Everything here is a checklist to verify on your own machine; details can differ between QNX SDP versions.

## Workflow

1. **Windows host**: install the QNX Software Center, then the SDP (free non-commercial license) and a QNX x86_64 VMware image if you have not already.
2. **Build** on the host with `qcc` (via Momentics terminal or the SDP shell), or build inside Momentics as a Makefile project.
3. **Deploy** the `priority_demo` binary to the VM.
4. **Run as root** in the VM; it writes a CSV.
5. **Copy the CSV back** to Windows and run `analyzer/analyze.py`.

## 1. Make the VM single-core (strongly recommended)

The program pins its threads to CPU 0, but the simplest guarantee is a one-processor VM: VMware, VM settings, Processors, 1 core. If the pin fails the program prints a warning and records `pin_failures` in the CSV.

## 2. Build

```sh
qcc -V                 # note a variant matching x86_64, e.g. gcc_ntox86_64 (SDP 7.x)
make VARIANT=<value>   # default is gcc_ntox86_64
```

If `qcc` is not found, the SDP environment script has not been loaded (`qnxsdp-env.sh` or `qnxsdp-env.bat`).

## 3. Deploy

In the VM, find its IP with `ifconfig`. Then either use Momentics' Target Navigator file transfer, or `scp priority_demo root@<vm-ip>:/tmp/` if an SSH server is running in the image. Make it executable with `chmod +x`.

## 4. Run

```sh
cd /tmp
./priority_demo run1.csv
```

- The first lines print the valid priority range and the calibration value.
- Expect the system to feel unresponsive for roughly 5 seconds: LOW runs at priority 10 under `SCHED_FIFO`.
- If `pthread_create` fails with a permission error, you are not root.

## 5. Analyze

Copy `run1.csv` to Windows (`scp` or Target Navigator), then:

```sh
python analyzer/analyze.py run1.csv
```

Compare the `resp (ms)` column with `expected (ms)` and look at the timeline. Small deltas (a few ms) are normal in a VM; large ones are worth investigating (is the VM single-core? is anything else busy?).

## Things to verify on the real system

- `clock_nanosleep` with `TIMER_ABSTIME` and `ThreadCtl(_NTO_TCTL_RUNMASK, ...)` behave as expected on your QNX version.
- The valid `SCHED_FIFO` range, and that 30/20/10 sit sensibly relative to system service priorities.
