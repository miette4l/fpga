# make_hello.py - create and build the BP decoder app on the Pynq-Z1 platform
#
# Run from command prompt:
#   C:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat -s make_bp.py
#
# Steps:
#   - 

import vitis

WORKSPACE = r"C:\Users\Holly\vitis_ws"

SRC = r"C:\Users\Holly\code\fpga\bp_c_microblazev"

PLATFORM = "plat_pynq"
DOMAIN = "standalone_microblaze_riscv_0"

client = vitis.create_client()
client.set_workspace(path=WORKSPACE)

# BP Application
xpfm = WORKSPACE + "\\" + PLATFORM + "\\export\\" + PLATFORM + "\\" + PLATFORM + ".xpfm"

print("Creating empty BP decoder app...")
app = client.create_app_component(
    name="bp_decoder",
    platform=xpfm,
    domain=DOMAIN,
    template="empty_application",
)
print("Importing decoder sources from ", SRC)
app.import_files(
    from_loc=SRC,
    files=["bp.c", "bp.h", "main.c", "pcm_data.h"],
    dest_dir_in_cmp="src",
)

print("Building BP decoder...")
app.build()

print("Done. The ELF is in", WORKSPACE + "\\bp_decoder\\build\\bp_decoder.elf")
vitis.dispose()