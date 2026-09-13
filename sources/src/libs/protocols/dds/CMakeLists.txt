cmake_minimum_required(VERSION 3.25)
project(uDdsProtocol C CXX)

# Cyclone DDS speaks the actual DDSI-RTPS wire protocol for us —
# discovery (SPDP/SEDP), reliability (HEARTBEAT/ACKNACK), fragmentation,
# ParameterList encode/decode, all of it — see dds_driver.hpp's class doc
# comment. So all that's left on the "protocol" side of the plugin's
# three-way split (see dds_plugin.hpp's class doc comment) is the one
# generic sample type every DDS.CMD topic is published/subscribed as —
# see idl/ucmdexec_dds.idl's doc comment — compiled once here via
# Cyclone's own idlc.
#
# uDdsProtocol is therefore an INTERFACE library: idlc_generate() below
# declares it and attaches the generated GenericSample.c/.h as INTERFACE
# sources, so it still "just links" into uDdsDriver exactly like a normal
# static library would.
find_package(CycloneDDS REQUIRED)

idlc_generate(TARGET ${PROJECT_NAME} FILES idl/ucmdexec_dds.idl WARNINGS no-implicit-extensibility)

target_link_libraries(${PROJECT_NAME} INTERFACE CycloneDDS::ddsc)
