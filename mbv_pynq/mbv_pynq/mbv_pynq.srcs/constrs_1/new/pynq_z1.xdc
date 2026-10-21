set_property -dict { PACKAGE_PIN H16 IOSTANDARD LVCMOS33 } [get_ports sys_clock]
create_clock -name sys_clk -period 8.000 [get_ports sys_clock]