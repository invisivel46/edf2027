# Source-level regression gate, not a whole-program reachability proof.
# SOURCE is guest_shader_bridge.cpp; the hooks split out of it (edf/hooks/*.cpp next to it) are read with it.
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
  message(FATAL_ERROR "Native hook bridge source is required")
endif()
file(READ "${SOURCE}" bridge)
get_filename_component(bridge_source_directory "${SOURCE}" DIRECTORY)
file(GLOB hook_sources "${bridge_source_directory}/edf/hooks/*.cpp")
list(SORT hook_sources)
foreach(hook_source IN LISTS hook_sources)
  file(READ "${hook_source}" hook_text)
  string(APPEND bridge "\n${hook_text}")
endforeach()
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
    "1,82135278,88ec3ca411c04d6e6c42d1a75f47fef969a602a8780901e8246b6067215981b4"
    "10,82135630,70cb0845eb1c6f937b7692a4ddb61c00c22857ef6522fe625dc325c98d095c8e"
    "11,82137978,31821517223075fc3761cff0f1c5e2807ccb9254e1d65a6ae21e7a72dec92d99"
    "13,821364C8,91d56f8a0cb044c8891250b280402019da34db429a69cf92ef19e9c48a357a64"
    "15,82135108,025633638206d561aad97cb464e059cf01097f7310afcc256367f99fd0c6f8ab"
    "16,82134EE8,e785754c9a26e14e5b1c96d468511df75fb4bd3f31378574d4e5a5c9d5e6b21e"
    "16,82135208,d6f270c33d2efcae33b6878fce9b1b1b9a77c61ec925a5e317e031c49fc8fac3"
    "17,82135530,140c81240ae0f74daa9f40b3f1d39ff865efb27cc5b052da8a5e9e7df0cf87c5"
    "19,82135720,cf8680141ff7fd7968032dbf5dc00154b1b47703d25ac63331ca87c136ae6436"
    "22,82134EB8,0aee8b0832fb371c97ece136e551f65edb796d15ec940a47c0f9ced02e246b77"
    "22,82135198,52707a39fc4b947c8604a735c614a65d0b26812193e65b275588b60ec44e43fa"
    "24,821355A8,464ff0a02eb25bfad5ed7b14a055de39e43c06db9a6ecf38387a27a022b5a5d0"
    "28,82135AB8,89ab9cc8000e55b83f1566110ca36c215190a86fafe386046dfee1d328aeb0df"
    "33,82135380,fc5b6fc0cb97fbe56c601334275d00fdb9fd10146a7113d70d0a79918e820163"
    "33,821356E0,7f1fc38ba0a2dfb059e3321c25cde09cd5c407f652f01f9d46457b974cd6d5c2"
    "33,82135BB0,a8784ebce553a67c0fa0975a77b99922a1190e0bf2ff314678613f58efa8dc28"
    "34,82135780,d53756fcbed56fca6dc0fb8581d7572050b94d8b4ea500292bc7108596cf7502"
    "35,82136478,367cd9d1edd6d62e7dba6fddabf5bb837ec363491dc47493dab752858adcb7ec"
    "43,82135B78,e27053c232323d2baee4280d508fc9b17796e9ff184176a94be800b62ddc9a34"
    "45,821364F8,30847d4fbd29248f6e824b5a2cd0cda2bf5b10bc75d5f9b606cc7aeb3c2e54e0"
    "47,82135578,6f88cd3e94db3748193af8df1b7d6872de2dde273f145b05c5be69e51be8bc60"
    "47,821356A0,1e56b7bac93078d8e0e3707846996944564bec4457b06b64ed54de6c653a6630"
    "48,82134F18,c9d58cf2e9ea53754458c6b8f94ec595e771367d68aee6677ffeb7b932d82127"
    "48,82135418,81070c28624b82667542c7e53da8167736543cf842121bf31d0bba248772f816"
    "51,82135078,d14291f7e553bed536b2ff9cb40e7a82e9757f5f5a696be5d2c3952919d44c70"
    "52,821355E8,5769e9e2f44af0b08211f24f897fb961d48c9eab474aaf957a90133f5d30f723"
    "52,82135B40,559d9059bbfe240c76c3456419e3d6df75b3c861819ac743655b8dd797460f0b"
    "6,82134F58,5e35714a032301cc0f2a4e4ac8dca1079c39d5a64d9992e688be8e3d369e4cc4"
    "60,82135750,e952a07e7e3b12ab90a701b9a127875b6a9c7224289285949142b558934e8497"
    "63,821353E8,659a0d500673fb36eb43cf694464b17257c58529dd7f58aaeb0cd8a98a2efba6"
    "63,82135670,af7c90c2b30aa9d65cd35c45abe5aad7b08951e9064cd78ccf22c49b95e4c3c7"
    "63,82135B08,2fc14f1b5c59bacb246218373cad564c114347a55e7c89feab1ad6bb731b2a08"
    "71,82134FE8,4e8d3588aec520741cb822ed5c2dd628a74b39915942fc764c2a830f25e8ac9b"
    "78,82135800,d32771bfa887d6dd20e27cc4cc7c2c71df6aceb78d28b01aa0bb43785c71375e"
    "8,821357C0,b9b0f3768c21040b1d874842c7bdfdbc89628d5d6addcf6371294900c3cda604"
    "80,821352E8,e41362e57fc344de0ee734bf15509d5c5546eabe68af82f01ed2e420488ed092"
    "33,82136700,f607f3549e6e4012020f48a117af724e27bcdba9603ff06dec4b2fe26bea5dd2"
    "4,82136888,6e479287004e4069f4a3eea331bc6f310541bc1257741813368e0a9435651cec"
    "35,82136C20,709b019af7cebfd30bd5f104a2e07c47d2a32b372a98527bee5cb08696cc2c32"
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
    "32,820B4250,4990a5ced66168588040da27c36713c0dba46403cabb9b6a64a3444148cea160"
    "6,820B4310,2e0ef3f721deee49ef72c3f5063755f6ddf3ee11b36c94b5eb2ef4c3ca994c84"
    "27,820B5FA8,5a0ea809f7b8dc753cc458c59e203573b243b3bac355b88d16a74b59bafd1305"
    "9,821BE8D0,7bd10d8b10bdfd5f43062b5e40f6775a8bcd5fc986695b5fec20a26be34f3815"
    "47,821A17F8,df58f4fdf28918c067e81e939e3a5779aa1af2c92b251252301e973a93acf77e"
    "37,821A19F0,8e821902791e464cb504cad27b2ac423b130d7576507116cbc8f3c1f8857662c"
    "60,821C8000,9af9624e0fd70129ec2c18d5cb98e334a8ac3e2f053cda6a6b3723c4af5ebea6"
    "24,821D96D8,426cfdf4a6fc8d6910ba55c615de44c9d5fe8bfb169a7a4e265e70bfabd4a6bc"
    "60,821D9600,761d1783b5ef3462589398f2e8d660a3465d36e6f511cc3cf54ecc1f6c19cf5a"
    "31,82149248,94fec491a3fbb653d93b0c3d25eaed52924b0a88f4e60827fe559b444150c379"
    "53,82149358,f3fb2cfe67666cac0b0e75c200b60e76a1c289cfc728cdef905d8758b5ca6fc3"
    "50,821498C8,43e19da937766c9e1e908331308f93496287edd8ef0be9ccf0b2bb3ef7fb8d6d"
    "35,82149608,d664be260de45b6b90a53cd97d1f310593ce802cd7b3e95446eaabc3f67912fc"
    "41,821A5080,2301b35d3af6d6932df2e48ff4f8a9a24701ad47bb842baae5fa85e57a698825"
    "47,821A3BA0,021aa2e41dcaeb36c5fafc2ec81eaafd86cdfc5fce6fa85d4017bf998ac971c2"
    "21,821C61D8,ffde89cd758dbd074028b26e9c334e4ffd599de260948644e8b6f75231f57f5c"
    "73,821C5FC8,f4ecf57600fcdf59ec96cd3aa75f07d3a56345ceebad6ec9b276bbd7f91c0622"
    "49,821C56C0,6f81fe681217fca4bac7e445fcb525cbeb5916f5eea116d86f4bb595521da317"
    "70,821C3178,d08e02d2dca8543a5d3370565e0c8fb4d2edd5fad88be62da1db1f2874628fb4"
    "47,821B0258,478f9bdd189e51e820b8ff862449d21f248e0045c89d3577d615203427f9a524"
    "6,820B4310,2e0ef3f721deee49ef72c3f5063755f6ddf3ee11b36c94b5eb2ef4c3ca994c84"
    "55,821C3BB8,ee4eb4d26396ac318f244850be72de07c050c169749558a62ea7a8e159719121"
    "13,821C7740,522ea93a42b46ca44becfffe2f73a3b5d3b7508dada1594150ca796c9c9d8cc5"
    "37,821C5730,d45f59c81b8a63396ec8d8b9efa6df4d4fc84d5317cef65383a69b27e9d60b44"
    "33,821C5488,f1c5e60cdd7182ddba31e6b56fe7ee6a8e9cddb2bdf2ad4a4271db032cd2d00e"
    "30,821C49A0,9737252bd9a8567a0e5dec48e17e1ba1b30af5ac7afff2caef620d0a8121d8f5"
    "52,820B5F38,9ef0ae481ff84090f9d640a9d7769bf632ed79451ad45b84e0c83a5525ad7bd0"
    "1,821D5930,8e63c4d7713ce7c25d7576fe3194a19f947acfd974aef9bd2747b38f0c828f48"
    "40,82149A90,d5673f9c40ac553ce8c136e3b373a66fd070a1585102b6ea5099cfa45b8d3861")
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
