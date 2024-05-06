# panic-wipe
Securely erase all connected drives as quickly as possible

### Usage:

Simply running `panic` will trigger a wipe of all devices. A basic wipe consists of 4 steps:

- Wipe the headers of all LUKS devices
	- For usres running HDDs, this step alone should be enough to render the data irrecoverable
- Secure discard all data on all storage devices
	- Only some SSDs support secure discard
- Normal (insecure) discard on all storage devices
	- This accounts for SSDs without secure discard support, but doesn't provide as strong security guarantees
- Writes a notice to every connected storage device informing any attacker in no uncertain terms that the data has been wiped irrecoverably
	- This should deter them from attempting to force you to unlock your drive (eg. legal threats or torture), since there's nothing to unlock
	- *Note:* If the attacker suspects that you still have a usable backup, they might try to force you to unlock your backup instead

The panic handler has multiple modes:

#### Shutdown modes

- Immediate poweroff (default)
	- This mode is specified by running `panic --immediate`, or by simply running `panic` with no options. It powers off the system as soon as the data is wiped
- Delayed poweroff
	- This mode is specified by running `panic --delay <seconds>`. It's identical to a normal panic, except it waits `<seconds>` before powering off, to allow the drive more time to TRIM discarded blocks
	- This mode is potentially more vulnerable to cold boot attacks, since contents of RAM aren't deleted until power is lost
- Reboot
	- This mode is specified by running `panic --reboot`. It's identical to normal panic, except it reboots the system instead of powering off, to allow the drive to TRIM discarded blocks after RAM is clearned
	- This mode trusts the BIOS to zero RAM immediately on power-on. On systems which fail to do this, cold boot attacks may be possible

#### Wipe modes

- Normal (default)
	- The default wipe behavior. This wipe mode should be effective on the widest range of drives
- OPAL (*untested*)
	- This wipe mode is specified by running `panic --erase=OPAL`. This option tells OPAL self-encrypting drives to perform a reset.

### Installation

Optionally, after installing this script, it can be bound to a keybind. This can allow you to more quickly trigger a panic wipe. Make sure the keybind isn't something you'll accidentally trigger, since this script will irrecoverably erase ALL data on EVERY storage device it can find

#### Qubes Installation

There are 2 ways to install this script onto a Qubes system. Both methods should work, but there may be situations where the 2nd method works better

**Method 1:**

- Copy this script into a trusted VM
	- Ideally this would be a disposable which has never been connected to the network
	- For extra security, you can use [qubes-clean](https://github.com/NobodySpecial256/qubes-clean) to copy this file into the disposable
- (Optional, but recommended) Review `panic.c` to make ensure no malicious behavior has been added
- Compile the script
	- (In your trusted VM) `gcc QubesIncoming/<source qube>/panic.c -o panic`
- Copy the compiled binary into dom0
	- (In a dom0 terminal) `qvm-run --pass-io 'cat /home/user/panic' | sudo tee /usr/local/bin/panic > /dev/null`
- Mark `panic` as executable
	- (In a dom0 terminal) `sudo chmod +x /usr/local/bin/panic`

**Method 2:**

- Install `gcc` into dom0
	- (In a dom0 terminal) `sudo qubes-dom0-update gcc`
- Copy this script into dom0
	- For extra security, you can use [qubes-clean](https://github.com/NobodySpecial256/qubes-clean) to copy this file into dom0
- (Optional, but recommended) Review `panic.c` to make ensure no malicious behavior has been added
- Compile the script
	- (In a dom0 terminal) `gcc panic.c -o panic`
- (Optional, but recommended) Copy or move the script to `/usr/local/bin` so that it's within `$PATH`


#### Non-Qubes Installation

Simply download the script, compile it, and put it into your system's `$PATH` (I recommend `/usr/local/bin`)

### Debugging

You can make sure everything is working properly by running `panic --dbg=dry-run`. This will make sure that the panic wipe is able to run successfully, without performing any destructive actions or powering off the system

For extra assurance, you can test the panic wipe in a VM or on a separate system without important data. For people willing to read some C code, browsing the source will reveal more debug options, as well as alternative wipe modes with different security properties

*Note: OPAL erase mode has not yet been tested*

### How to copy files to dom0

The Qubes official documentation has information about copying files to dom0: https://www.qubes-os.org/doc/how-to-copy-from-dom0/#copying-to-dom0

For better security, you should download this into a disposable VM, to prevent a compromised qube from tampering with the data locally

For the best security, you should use [qubes-clean](https://github.com/NobodySpecial256/qubes-clean) to copy this script into dom0
