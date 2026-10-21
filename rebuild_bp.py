import vitis
client = vitis.create_client()
client.set_workspace(path=r"C:\Users\Holly\vitis_ws")
client.get_component(name="bp_decoder").build()
vitis.dispose()