# Source-level regression gate, not a whole-program reachability proof.
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "Native hook bridge source is required")
endif()
file(READ "${SOURCE}" bridge)
foreach(address IN ITEMS 821409A0 82140E98 821512D8 8214E640
                         8213BD90 8213C410 8214EE50 8214EFF8)
  string(FIND "${bridge}" "REX_HOOK_RAW(sub_${address})" hook)
  if(hook EQUAL -1)
    message(FATAL_ERROR "Required native hook ${address} is missing")
  endif()
  string(FIND "${bridge}" "__imp__sub_${address}" legacy)
  if(NOT legacy EQUAL -1)
    message(FATAL_ERROR "Legacy original entry ${address} reintroduced in native bridge")
  endif()
endforeach()
message(STATUS "Eight native-only hook boundaries verified")

# The inline-writer extent/caller pair is valid only for this audited producer.
# Keep this distinct from the native-only hooks above: its CPU body is retained.
get_filename_component(bridge_directory "${SOURCE}" DIRECTORY)
get_filename_component(project_directory "${bridge_directory}/../.." ABSOLUTE)
file(READ "${project_directory}/generated/default/edf2017_recomp.10.cpp" generated)
string(REPLACE "\r\n" "\n" generated "${generated}")
string(FIND "${generated}" "DEFINE_REX_FUNC(sub_8242D2B0) {" begin)
if(begin LESS 0)
  message(FATAL_ERROR "Inline index writer producer missing")
endif()
string(SUBSTRING "${generated}" ${begin} -1 tail)
string(FIND "${tail}" "\nDEFINE_REX_FUNC(" end)
if(end LESS 0)
  message(FATAL_ERROR "Inline index writer producer boundary missing")
endif()
string(SUBSTRING "${tail}" 0 ${end} body)
string(SHA256 fingerprint "${body}")
if(NOT fingerprint STREQUAL "fa4a8b883fc51da5dc5a94fcabb623aa45498acff9a192ccb9b549f707e5d96f")
  message(FATAL_ERROR "Inline index writer changed; re-audit lock/store/unlock interval")
endif()
foreach(address IN ITEMS 8242D2B0 82134A78 82134AD8)
  string(FIND "${bridge}" "REX_HOOK_RAW(sub_${address})" hook)
  if(hook LESS 0)
    message(FATAL_ERROR "Inline index writer hook ${address} missing")
  endif()
endforeach()
message(STATUS "Audited six-store inline index writer fingerprint verified")

# Constant-time cache maintenance preserves the endpoint of this exact body;
# its dcbf/sync instructions currently have no generated host operation.
file(READ "${project_directory}/generated/default/edf2017_recomp.5.cpp" generated)
string(REPLACE "\r\n" "\n" generated "${generated}")
string(FIND "${generated}" "DEFINE_REX_FUNC(sub_82141AB8) {" begin)
if(begin LESS 0)
  message(FATAL_ERROR "Cache flush original missing")
endif()
string(SUBSTRING "${generated}" ${begin} -1 tail)
string(FIND "${tail}" "\nDEFINE_REX_FUNC(" end)
if(end LESS 0)
  message(FATAL_ERROR "Cache flush original boundary missing")
endif()
string(SUBSTRING "${tail}" 0 ${end} body)
string(SHA256 fingerprint "${body}")
if(NOT fingerprint STREQUAL "fea0e017e1244dd6f5c58a9d95941fcf47331c1e48c61ebcb2350ddb8b55f9ae")
  message(FATAL_ERROR "Cache flush CPU effects changed; re-audit native constant-time endpoint")
endif()
string(FIND "${bridge}" "REX_HOOK_RAW(sub_82141AB8)" hook)
if(hook LESS 0)
  message(FATAL_ERROR "Native cache flush hook missing")
endif()
message(STATUS "Native cache flush original fingerprint verified")

# Native scene groups retain setup callbacks and replace instance uploads/draws.
# The event-based source catalog and clean-state constant-bank shortcut depend
# on these complete CPU bodies;
# FE358's packet-free CPU prefix is separately gated by extract-native-indexed-tail.
foreach(entry IN ITEMS
    "36,8213BA98,8f11248594563e3bca0be3d3ca72618b4b2997b6b6265ceda6a574684a1fcb74"
    "38,821B8E48,1382e9cfc357408d69ec2b59776db047cc256215fc6977d8d371b6e8cd1ebf36"
    "36,821C4EB8,5312079e319b91538306ab6393359c3848e6d9a7bb26c292e1a95c7f3bcbb320"
    "5,821C5D28,64f194a3d02f9f58c77b33fa25e4c74e7b29ece4fe5fc9f151132c8181caecc7"
    "45,821A1628,d37bcf14ed35e4aacbcfc69762da6582d5e22987599ce00f1c559672fd599e2b"
    "51,821A1678,fef9d4b560b8654afeb19a2b0e504354cccaee24f36bd0c93e771ff44e3fc27e"
    "57,820B4038,b6ea29e824776b7faf93003f12ac4bdea8cda6e292e4df65ef28904fa1febfb5"
    "72,821C3070,4d7d04648825c836cec6b48c8eef03e06318ae38151ec1b3c59f17a0ee5e53e3"
    "48,821C33E8,38a22b7acacf784d882e58fde25cea19b55ac83f86e0d7f495417a6cc89b3289"
    "76,821B0198,9270dcafb961e631d5e0a617f00b8095cc0d7f2abc1539cb9c3368330ddcd217"
    "66,820B2670,51b8250b3a3144ccc9f7d2b65eec5fa6e769dcee4c84928e76df7cd01abb8f5e"
    "63,821BEF10,840d643155039a1f7260a6a0a3db3b7fa3c947ab8f1629972d7a84711f74fd13"
    "76,821C0B88,637f9608782f140056b597ddad38da29626162d46dd5db4401b15a0f815e74fd"
    "57,821C0C00,60ad66a94a27371d47a8a7b1af5acdb104343433f09deb491f89cd9717093426"
    "0,821C07B8,d35880fbf62f6f692c95aa8c9c34fab6c8ab22a002226525628b2ba213e00a06"
    "55,821C3BB8,ee4eb4d26396ac318f244850be72de07c050c169749558a62ea7a8e159719121"
    "67,821BEE68,56c6dcc2ff299de31ef654c8fc784eca2873808899035336924bb0b6d2fbb5bf"
    "16,820B33B0,f294bc2b8ecc2f155a536beafcefff60dff90114c375ba071f0e23fe42237ff0"
    "68,820B2AC0,ef9fbb74cef32ebda312f878eae2d086297e1fbc10f9ce72566a4d137f25bec1"
    "72,820B2DF8,d4e57e4fe29121a00193ba5e42cb64f0c918e1dbe045ac05cf04cb04b7fef9e5"
    "80,821BEDF0,4fd02a49b3045aefea6c223fe43ae6e9dcbb0e3c776d469ba07db902f8014eb9"
    "47,821BED40,b36646270aec17e8c3b063bb20c828bee2f7aa7831bf876300d3b99d906c2adb"
    "24,821D96D8,426cfdf4a6fc8d6910ba55c615de44c9d5fe8bfb169a7a4e265e70bfabd4a6bc"
    "60,821D9600,761d1783b5ef3462589398f2e8d660a3465d36e6f511cc3cf54ecc1f6c19cf5a"
    "31,82149248,94fec491a3fbb653d93b0c3d25eaed52924b0a88f4e60827fe559b444150c379")
  string(REPLACE "," ";" fields "${entry}")
  list(GET fields 0 file)
  list(GET fields 1 address)
  list(GET fields 2 expected)
  file(READ "${project_directory}/generated/default/edf2017_recomp.${file}.cpp" generated)
  string(REPLACE "\r\n" "\n" generated "${generated}")
  string(FIND "${generated}" "DEFINE_REX_FUNC(sub_${address}) {" begin)
  if(begin LESS 0)
    message(FATAL_ERROR "Native scene original ${address} missing")
  endif()
  string(SUBSTRING "${generated}" ${begin} -1 tail)
  string(FIND "${tail}" "\nDEFINE_REX_FUNC(" end)
  if(end LESS 0)
    message(FATAL_ERROR "Native scene original ${address} boundary missing")
  endif()
  string(SUBSTRING "${tail}" 0 ${end} body)
  string(SHA256 fingerprint "${body}")
  if(NOT fingerprint STREQUAL expected)
    message(FATAL_ERROR "Native scene original ${address} changed; re-audit queue/constant effects")
  endif()
endforeach()
message(STATUS "Native scene group, source lifecycle and constant upload originals verified")
