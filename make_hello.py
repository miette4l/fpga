# make_hello.py - create the Vitis platform and Hello World app for Pynq-Z1
# MicroBlaze V design
# Can't use IDE menus so making script...
#
# Run from command prompt:
#   C:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat -s make_hello.py
#
# Steps:
#   - create hardware platform
#   - create new application
#   - build library from platform
#   - use Vitis hello world template to make C program

import vitis

WORKSPACE = r"C:\Users\Holly\vitis_ws"

XSA = r"C:\Users\Holly\mbv_pynq\mbv_pynq_80.xsa"

PLATFORM = "plat_pynq"
CPU = "microblaze_riscv_0"
DOMAIN = "standalone_" + CPU

client = vitis.create_client()
client.set_workspace(path=WORKSPACE)

# 1. Platform from the .xsa
print("Creating platform...")
platform = client.create_platform_component(
    name=PLATFORM,
    hw_design=XSA,
    os="standalone",
    cpu=CPU,
    domain_name=DOMAIN,
)
print("Building platform...")
platform.build()

# 2. Hello world application on that platform
xpfm = WORKSPACE + "\\" + PLATFORM + "\\export\\" + PLATFORM + "\\" + PLATFORM + ".xpfm"
print("Creating hello world app on", xpfm)
app = client.create_app_component(
    name="hello_world",
    platform=xpfm,
    domain=DOMAIN,
    template="hello_world",
)
print("Building Hello World...")
app.build()

print("Done. Open the workspace in Vitis IDE to run on board...???")
vitis.dispose()