cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_SOURCE_DIR}/../common/PackageContext.cmake")

add_library(cd_pixel_desk_core STATIC Model.cpp Renderer.cpp ClockInstance.cpp)
target_link_libraries(cd_pixel_desk_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_spring_core STATIC SpringClock.cpp)
target_link_libraries(cd_pixel_desk_spring_core PUBLIC cd_pixel_desk_toys_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_spring_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_spring_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_spring_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Reuse the production Box2D model verbatim, specializing only its rail lengths.
# Keep the original family untouched; both pixel cases have independent apertures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/../marble-clock/Model.cpp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../marble-clock/Model.cpp" pixel_marble_model)
string(REPLACE "result.width = compact ? 720 : 1080;" "result.width = compact ? 1000 : 1680;"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "result.pivot = compact ? 474 : 588;" "result.pivot = compact ? 680 : 1000;"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "const double end = compact ? 623 : 947, turn = compact ? 672 : 996;"
  "const double end = result.width - 97, turn = result.width - 48;"
  pixel_marble_model "${pixel_marble_model}")
# Keep enough headroom above the feed rail for the larger collision diameter.
string(REPLACE "{{26, 92}, {result.width - 26, 92}, 3}" "{{26, 50}, {result.width - 26, 50}, 3}"
  pixel_marble_model "${pixel_marble_model}")
# The original return lip overhangs the enlarged pile and traps older balls
# above it. End the chute just inside the bin so the full pile has headroom.
string(REPLACE "{result.pivot - 140, 220}" "{result.pivot - 70, 220}"
  pixel_marble_model "${pixel_marble_model}")
# Lift both feed lanes together, preserving their turn clearance while giving
# the enlarged collection pile a second full row below the return lane.
string(REPLACE "{92, 122}, {end, 153}" "{92, 94}, {end, 125}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{turn, 127}, {turn, 180}" "{turn, 99}, {turn, 152}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{turn, 180}, {turn - 2, 191}" "{turn, 152}, {turn - 2, 163}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{turn - 2, 191}, {turn - 10, 198}" "{turn - 2, 163}, {turn - 10, 170}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{turn - 10, 198}, {end + 3, 202}" "{turn - 10, 170}, {end + 3, 174}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{end + 3, 202}, {result.pivot + 47, 212}" "{end + 3, 174}, {result.pivot + 47, 184}"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "{result.pivot + 47, 212}, {result.pivot - 70, 220}"
  "{result.pivot + 47, 184}, {result.pivot - 70, 192}" pixel_marble_model "${pixel_marble_model}")
# Keep the visible and physical radii identical and fit the sixty-ball minute.
string(REPLACE "#include \"plugins/marble-clock/Clock.h\"" "#include \"plugins/pixel-desk/MarbleClock.h\""
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "kRadius" "pixeldesk::kMarbleRadius" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "/ 18)" "/ 26)" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "+ 17 + column * 18 + (row % 2) * 8.5" "+ 20 + column * 26 + (row % 2) * 13"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "- 13 - row * 15.8" "- 17 - row * 22.5" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "add({85, 106}, true);" "add({108, 72}, true);" pixel_marble_model "${pixel_marble_model}")
# The longer pixel lanes need time for the sixtieth ball to reach the physical
# return outlet. Queue the next seconds instead of deleting that last ball or
# silently dropping new releases during the longer minute transition.
string(REPLACE "drainTime_ > 8" "drainTime_ > 12" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "pending_ >= 8" "pending_ >= 16" pixel_marble_model "${pixel_marble_model}")
# Keep the tip's opening clearance independent of the two different flap lengths.
string(REPLACE "Point meters(Point p)" "double pixelClosedAngle(const Geometry& shape) { return -std::atan(5.0 / (shape.pivot - shape.left)); }\n\ndouble pixelOpenAngle(const Geometry& shape) { return -std::atan(43.0 / (shape.pivot - shape.left)); }\n\nPoint meters(Point p)"
  pixel_marble_model "${pixel_marble_model}")
string(REPLACE "kClosedAngle" "pixelClosedAngle(geometry_)" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "kOpenAngle" "pixelOpenAngle(geometry_)" pixel_marble_model "${pixel_marble_model}")
string(REPLACE "std::tan(pixelClosedAngle(geometry_)) * (x - shape.pivot)"
  "std::tan(pixelClosedAngle(shape)) * (x - shape.pivot)" pixel_marble_model "${pixel_marble_model}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/MarbleModel.cpp" CONTENT "${pixel_marble_model}" @ONLY)
add_library(cd_pixel_desk_marble_core STATIC MarbleClock.cpp "${CMAKE_CURRENT_BINARY_DIR}/MarbleModel.cpp")
target_include_directories(cd_pixel_desk_marble_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../marble-clock")
target_link_libraries(cd_pixel_desk_marble_core PUBLIC cd_marble_clock_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_marble_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_marble_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_marble_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_precision_core STATIC PrecisionClock.cpp)
target_link_libraries(cd_pixel_desk_precision_core PUBLIC cd_precision_clocks_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_precision_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_precision_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_precision_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_calendar_core STATIC CalendarRenderer.cpp CalendarInstance.cpp)
target_link_libraries(cd_pixel_desk_calendar_core PUBLIC cd_mechanical_calendar_core)
target_include_directories(cd_pixel_desk_calendar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_calendar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_calendar_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_lunar_core STATIC LunarInstance.cpp LunarRenderer.cpp)
target_link_libraries(cd_pixel_desk_lunar_core PUBLIC cd_lunar_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_lunar_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_lunar_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_lunar_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_weather_core STATIC WeatherInstance.cpp WeatherRenderer.cpp)
target_link_libraries(cd_pixel_desk_weather_core PUBLIC cd_weather_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_weather_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_weather_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_weather_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_air_core STATIC AirInstance.cpp AirRenderer.cpp)
target_link_libraries(cd_pixel_desk_air_core PUBLIC cd_air_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_air_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_air_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_air_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_monitor_core STATIC MonitorRenderer.cpp ../system-info-suite/Monitor.cpp)
target_link_libraries(cd_pixel_desk_monitor_core PUBLIC cd_mechanical_pixels_core CyberCanvas::Motion
  PRIVATE d2d1 dwrite windowscodecs ole32 user32)
target_include_directories(cd_pixel_desk_monitor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_monitor_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_monitor_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

find_package(nlohmann_json 3.12 CONFIG REQUIRED)
find_package(cpr 1.14.2 EXACT CONFIG REQUIRED)
find_package(tinyxml2 11 CONFIG REQUIRED)
include("${CMAKE_CURRENT_LIST_DIR}/../common/ProductivityVolume.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../common/ContentText.cmake")
add_library(cd_pixel_desk_productivity_core STATIC ProductivityRenderer.cpp DiskRenderer.cpp TimerRenderer.cpp CountdownRenderer.cpp
  ../productivity-suite/Model.cpp ../productivity-suite/Instance.cpp)
target_link_libraries(cd_pixel_desk_productivity_core PUBLIC cd_pixel_desk_basic_clock_core cd_mechanical_pixels_core CyberCanvas::Motion
  PRIVATE cd_productivity_volume nlohmann_json::nlohmann_json cd_content_text d2d1 dwrite windowscodecs ole32 shell32 user32)
target_include_directories(cd_pixel_desk_productivity_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_productivity_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_productivity_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_input_core STATIC InputInstance.cpp InputRenderer.cpp)
target_link_libraries(cd_pixel_desk_input_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_input_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_input_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_input_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_album_core STATIC AlbumInstance.cpp AlbumRenderer.cpp)
target_link_libraries(cd_pixel_desk_album_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_album_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_album_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_album_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_player_core STATIC PlayerInstance.cpp PlayerRenderer.cpp)
target_link_libraries(cd_pixel_desk_player_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_player_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_player_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_player_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_matrix_core STATIC MatrixInstance.cpp MatrixRenderer.cpp ContinuousSpectrum.cpp)
target_link_libraries(cd_pixel_desk_matrix_core PUBLIC cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_matrix_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_matrix_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_matrix_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Preserve production audio/physics behavior; specialize only approved panel geometry.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../beat-machine/Model.cpp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../beat-machine/Model.cpp" pixel_beat_model)
string(REPLACE "return compact_ ? 720 : kWidth;" "return compact_ ? 720 : 1180;" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "92 + i * 105. : 132 + i * 155." "65 + i * 115. : 117 + i * 174." pixel_beat_model "${pixel_beat_model}")
string(REPLACE "217 + i * 134. : 330 + i * 220." "140 + i * 280. : 300 + i * 410." pixel_beat_model "${pixel_beat_model}")
string(REPLACE "{{{505, 145}, {554, 195}, {625, 124}}}" "{{{499, 96}, {584, 160}, {645, 213}}}" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "{{{766, 142}, {865, 195}, {932, 126}}}" "{{{869, 102}, {1060, 153}, {1133, 199}}}" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "compact_ ? 20 : 24" "compact_ ? 28 : 30" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "compact_ ? 36." "compact_ ? 27." pixel_beat_model "${pixel_beat_model}")
string(REPLACE ": 52.)" ": 30.)" pixel_beat_model "${pixel_beat_model}")
# Opposed levers mirror the approved two-sided mechanism, including its collision capsule and motor.
string(REPLACE ".angle = .25, .gravityScale = 0" ".angle = i == 1 ? -.25 : .25, .gravityScale = 0" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "meters({compact_ ? 70. : 105., 0})" "meters({(i == 1 ? -1. : 1.) * (compact_ ? 70. : 105.), 0})" pixel_beat_model "${pixel_beat_model}")
string(REPLACE ".lowerAngle = -.55, .upperAngle = .45" ".lowerAngle = i == 1 ? -.45 : -.55, .upperAngle = i == 1 ? .55 : .45" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "const double target = paddle.until > time_ ? -.22 : .25 - middle_ * .65;"
  "const double drive = paddle.until > time_ ? -.22 : .25 - middle_ * .65;\n    const double target = &paddle == &paddles_[1] ? -drive : drive;"
  pixel_beat_model "${pixel_beat_model}")
# Opposed levers mirror the approved mechanism, including capsule and motor.
string(REPLACE ".angle = .25, .gravityScale = 0" ".angle = i == 1 ? -.25 : .25, .gravityScale = 0" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "meters({compact_ ? 70. : 105., 0})" "meters({(i == 1 ? -1. : 1.) * (compact_ ? 70. : 105.), 0})" pixel_beat_model "${pixel_beat_model}")
string(REPLACE ".lowerAngle = -.55, .upperAngle = .45" ".lowerAngle = i == 1 ? -.45 : -.55, .upperAngle = i == 1 ? .55 : .45" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "const double target = paddle.until > time_ ? -.22 : .25 - middle_ * .65;"
  "const double drive = paddle.until > time_ ? -.22 : .25 - middle_ * .65;\n    const double target = &paddle == &paddles_[1] ? -drive : drive;"
  pixel_beat_model "${pixel_beat_model}")
string(REPLACE "(compact_ ? 28 : 30) / kUnits" "(i == 2 ? 18 : compact_ ? 28 : 30) / kUnits" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "compact_ ? 6.5 : 8" "compact_ ? 8.5 : 12.5" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "(i == 4 ? 7. : 51.)" "(i == 4 ? 7. : 56.)" pixel_beat_model "${pixel_beat_model}")
string(REPLACE "const physics::Material metal" "for (size_t i = 3; i <= 5; ++i) { rails_[i].radius = 6; }\n  const physics::Material metal"
  pixel_beat_model "${pixel_beat_model}")
string(REPLACE "pool = compact ? 595 : 955;" "pool = compact ? 595 : 1055;" pixel_beat_model "${pixel_beat_model}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BeatModel.cpp" CONTENT "${pixel_beat_model}" @ONLY)
add_library(cd_pixel_desk_beat_core STATIC BeatRenderer.cpp "${CMAKE_CURRENT_BINARY_DIR}/BeatModel.cpp"
  ../beat-machine/AudioMotion.cpp BeatInstance.cpp)
target_link_libraries(cd_pixel_desk_beat_core PUBLIC cd_mechanical_pixels_core CyberCanvas::Physics2D)
target_include_directories(cd_pixel_desk_beat_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../.." "${CMAKE_CURRENT_SOURCE_DIR}/../beat-machine")
target_compile_options(cd_pixel_desk_beat_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_beat_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Specialize only the production pendant geometry for the approved longer pixel chime.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../physics-toys/Model.cpp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../physics-toys/Model.cpp" pixel_chime_model)
string(REPLACE "Size{260, 290}" "Size{270, 410}" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "std::array{.58, .42, .66}" "std::array{.85, .65, .95}" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "(i == 1   ? .28" "(i == 1   ? .42" pixel_chime_model "${pixel_chime_model}")
string(REPLACE ": i == 2 ? .31" ": i == 2 ? .46" pixel_chime_model "${pixel_chime_model}")
string(REPLACE ": .24)" ": .36)" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "mechanical_ ? .32" "mechanical_ ? .48" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "{-.14, -.24}, {.14, -.24}, {.30, .20}, {-.30, .20}"
  "{-.21, -.36}, {.21, -.36}, {.45, .30}, {-.45, .30}" pixel_chime_model "${pixel_chime_model}")
string(REPLACE ".45 + i * .85" ".35 + i * .95" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "tailLength = mechanical_ ? .18" "tailLength = mechanical_ ? .65" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "Size{.12, .25}" "Size{.16, .35}" pixel_chime_model "${pixel_chime_model}")
# The tall bird-and-leaf chime uses an independent arrangement. The production
# model's compact flag selects this geometry only for Kind::Chime.
string(REPLACE "Size{270, 410}" "Size{compact_ ? 313. : 270., compact_ ? 600. : 410.}" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "std::array{.85, .65, .95}" "(compact_ ? std::array{.90, 1.65, 1.05} : std::array{.85, .65, .95})" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "mechanical_ ? .70 : .46" "mechanical_ ? (compact_ ? 1.20 : .70) : .46" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "const double x = mechanical_ ? .35 + i * .95 : .70 + i * .60;" "const double x = mechanical_ ? (compact_ ? std::array{.63, 1.64, 2.58}[i] : .35 + i * .95) : .70 + i * .60;\n      const double suspension = compact_ && mechanical_ ? std::array{1.34, 1.20, 1.30}[i] : top;" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "mechanical_ ? .48" "mechanical_ ? (compact_ ? .52 : .48)" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "top + lengths[i]" "suspension + lengths[i]" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "{x, top}, {0, -half.height}" "{x, suspension}, {0, -half.height}" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "mechanical_ ? .65 : .32" "mechanical_ ? (compact_ ? std::array{.85, .85, .78}[i] : .65) : .32" pixel_chime_model "${pixel_chime_model}")
string(REPLACE "Size{.16, .35}" "Size{compact_ && i == 1 ? .22 : .16, compact_ ? (i == 1 ? .45 : .40) : .35}" pixel_chime_model "${pixel_chime_model}")
# A distance joint suppresses collision between its two bodies. Keep the
# enclosure separate from the unshaped suspension body so walls really collide.
string(REPLACE "ground_ = world_->createBody({.type = physics::BodyType::Static});"
  "ground_ = world_->createBody({.type = physics::BodyType::Static});\n  const auto enclosure = world_->createBody({.type = physics::BodyType::Static});"
  pixel_chime_model "${pixel_chime_model}")
string(REPLACE "world_->box(ground_," "world_->box(enclosure," pixel_chime_model "${pixel_chime_model}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PixelToysModel.cpp" CONTENT "${pixel_chime_model}" @ONLY)
add_library(cd_pixel_desk_toys_core STATIC "${CMAKE_CURRENT_BINARY_DIR}/PixelToysModel.cpp")
target_include_directories(cd_pixel_desk_toys_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../physics-toys")
target_link_libraries(cd_pixel_desk_toys_core PUBLIC cd_physics_toys_core)
target_compile_options(cd_pixel_desk_toys_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_toys_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
add_library(cd_pixel_desk_chime_core STATIC ChimeRenderer.cpp ChimeInstance.cpp)
target_include_directories(cd_pixel_desk_chime_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../physics-toys")
target_link_libraries(cd_pixel_desk_chime_core PUBLIC cd_pixel_desk_toys_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_chime_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_chime_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_chime_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_sign_core STATIC SignRenderer.cpp SignLayout.cpp ../mechanical-sign/Model.cpp SignInstance.cpp)
target_link_libraries(cd_pixel_desk_sign_core PUBLIC cd_mechanical_pixels_core icu)
target_include_directories(cd_pixel_desk_sign_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_sign_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_sign_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_hourglass_core STATIC HourglassLayout.cpp HourglassRenderer.cpp HourglassInstance.cpp)
target_link_libraries(cd_pixel_desk_hourglass_core PUBLIC cd_hourglass_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_hourglass_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_hourglass_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_hourglass_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_basic_clock_core STATIC BasicClock.cpp)
target_link_libraries(cd_pixel_desk_basic_clock_core PUBLIC cd_clock_core cd_mechanical_pixels_core)
target_include_directories(cd_pixel_desk_basic_clock_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_basic_clock_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_basic_clock_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Reuse production carving and rigid-body code; register its enclosure to the
# plaque aperture while preserving glyph coordinates and all chip dynamics.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../brick-clock/Model.cpp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../brick-clock/Model.cpp" pixel_brick_model)
string(REPLACE "{5.1, .05}" "{8.85, .05}" pixel_brick_model "${pixel_brick_model}")
string(REPLACE "{-.05, 1.25}" "{-3.85, 1.25}" pixel_brick_model "${pixel_brick_model}")
string(REPLACE "{10.05, 1.25}" "{13.85, 1.25}" pixel_brick_model "${pixel_brick_model}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PixelBrickModel.cpp" CONTENT "${pixel_brick_model}" @ONLY)
add_library(cd_pixel_desk_brick_core STATIC PixelBrick.cpp BrickInstance.cpp "${CMAKE_CURRENT_BINARY_DIR}/PixelBrickModel.cpp")
target_link_libraries(cd_pixel_desk_brick_core PUBLIC cd_pixel_desk_basic_clock_core cd_brick_clock_core)
target_include_directories(cd_pixel_desk_brick_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../.." PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../brick-clock")
target_compile_options(cd_pixel_desk_brick_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_brick_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

add_library(cd_pixel_desk_pond_core STATIC PondRenderer.cpp PondInstance.cpp)
target_link_libraries(cd_pixel_desk_pond_core PUBLIC cd_pixel_desk_basic_clock_core cd_koi_core)
target_include_directories(cd_pixel_desk_pond_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_pond_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_pond_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Reuse the production persistence, audio grouping and transactional controller.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../ambience-suite/Instance.cpp")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../ambience-suite/Instance.cpp" pixel_ambience_instance)
string(REPLACE "APPEARANCE(spec.rows > 1)" "APPEARANCE(false)" pixel_ambience_instance "${pixel_ambience_instance}")
string(REPLACE "{\"show_info\", CD_SETTING_TOGGLE, spec.rows > 1}" "{\"show_info\", CD_SETTING_TOGGLE, false}" pixel_ambience_instance "${pixel_ambience_instance}")
string(REPLACE "0, 0, \"pothos\", 0, 0" "0, 0, spec.columns == 1 ? \"succulent\" : \"pothos\", 0, 0" pixel_ambience_instance "${pixel_ambience_instance}")
string(REPLACE "if (spec.kind == Kind::Noise && self->ambient.open) {"
  "if (spec.kind == Kind::Noise && !self->ambient.open) { self->model.audioError = 1; }\n    if (spec.kind == Kind::Noise && self->ambient.open) {"
  pixel_ambience_instance "${pixel_ambience_instance}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/PixelAmbienceInstance.cpp" CONTENT "${pixel_ambience_instance}" @ONLY)
add_library(cd_pixel_desk_ambience_core STATIC AmbienceRenderer.cpp ../ambience-suite/Model.cpp "${CMAKE_CURRENT_BINARY_DIR}/PixelAmbienceInstance.cpp")
target_include_directories(cd_pixel_desk_ambience_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../.." PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ambience-suite")
target_link_libraries(cd_pixel_desk_ambience_core PUBLIC cd_pixel_desk_basic_clock_core CyberCanvas::Noise)
target_compile_options(cd_pixel_desk_ambience_core PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_ambience_core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json" catalog)
string(JSON count LENGTH "${catalog}")
math(EXPR last "${count} - 1")
set(packages)
foreach(index RANGE 0 ${last})
  foreach(key id folder art family)
    string(JSON ${key} GET "${catalog}" ${index} ${key})
  endforeach()
  string(REPLACE "-" "_" target "cd_pixel_desk_${folder}")
  set(extra)
  set(art_dependencies)
  if(family STREQUAL "spring")
    add_library(${target} MODULE SpringPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_spring_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "chime")
    add_library(${target} MODULE ChimePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_chime_core)
  elseif(family STREQUAL "marble")
    add_library(${target} MODULE MarblePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_marble_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "hourglass")
    add_library(${target} MODULE HourglassPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_hourglass_core)
  elseif(family STREQUAL "sign")
    add_library(${target} MODULE SignPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_sign_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "beat")
    add_library(${target} MODULE BeatPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_beat_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "matrix" OR family STREQUAL "spectrum")
    add_library(${target} MODULE MatrixPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_matrix_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "player")
    add_library(${target} MODULE PlayerPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_player_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "album")
    add_library(${target} MODULE AlbumPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_album_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "input")
    add_library(${target} MODULE InputPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_input_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "productivity" OR family STREQUAL "disk" OR family STREQUAL "timer" OR family STREQUAL "countdown")
    add_library(${target} MODULE ProductivityPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_productivity_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "monitor")
    add_library(${target} MODULE MonitorPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_monitor_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "air")
    add_library(${target} MODULE AirPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_air_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "weather")
    add_library(${target} MODULE WeatherPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_weather_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "lunar")
    add_library(${target} MODULE LunarPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_lunar_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "calendar")
    add_library(${target} MODULE CalendarPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_calendar_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "plant" OR family STREQUAL "noise")
    add_library(${target} MODULE AmbiencePlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_ambience_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "koi")
    add_library(${target} MODULE PondPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_pond_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "brick")
    add_library(${target} MODULE BrickPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_brick_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "basic-clock")
    add_library(${target} MODULE BasicClockPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_basic_clock_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  elseif(family STREQUAL "precision")
    add_library(${target} MODULE PrecisionPlugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_precision_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  else()
    add_library(${target} MODULE Plugin.cpp)
    target_link_libraries(${target} PRIVATE cd_pixel_desk_core)
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png")
  endif()
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" descriptor)
  string(REGEX MATCH "version: \"([^\"]+)\"" version_match "${descriptor}")
  set(version "${CMAKE_MATCH_1}")
  if(NOT version)
    message(FATAL_ERROR "Pixel Desk descriptor has no version: ${folder}")
  endif()
  target_compile_definitions(${target} PRIVATE CD_PLUGIN_BUILD CD_PIXEL_DESK_ID="${id}" CD_PIXEL_DESK_VERSION="${version}")
  target_compile_options(${target} PRIVATE /utf-8 /W4)
  target_link_options(${target} PRIVATE /INCREMENTAL:NO)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME plugin
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${id}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/objects/${folder}")
  set(input "${CMAKE_CURRENT_BINARY_DIR}/package-input/${id}")
  set(extra)
  if(family STREQUAL "spring" OR family STREQUAL "chime")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt")
  elseif(family STREQUAL "hourglass")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
  else()
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/provenance.json" "${input}/assets")
  endif()
  if(family STREQUAL "spring")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/body.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/${art}/provenance.json" "${input}/assets")
  endif()
  string(JSON runtime_assets ERROR_VARIABLE no_runtime_assets GET "${catalog}" ${index} runtimeAssets)
  if(no_runtime_assets STREQUAL "NOTFOUND")
    string(JSON asset_count LENGTH "${runtime_assets}")
    math(EXPR asset_last "${asset_count} - 1")
    foreach(asset_index RANGE 0 ${asset_last})
      string(JSON runtime_name MEMBER "${runtime_assets}" ${asset_index})
      string(JSON source_asset GET "${runtime_assets}" "${runtime_name}")
      list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/${source_asset}")
      list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/${source_asset}" "${input}/assets/${runtime_name}")
    endforeach()
  endif()
  if(family STREQUAL "marble" OR family STREQUAL "beat" OR family STREQUAL "brick")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_BOX2D_LICENSE}" "${input}/licenses/Box2D-MIT.txt")
  endif()
  if(family STREQUAL "plant" OR family STREQUAL "noise")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_FASTNOISELITE_LICENSE}" "${input}/licenses/FastNoiseLite-MIT.txt"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC.txt")
  endif()
  if(family STREQUAL "koi")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/koi-suite/licenses/animal-proc-anim-MIT.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "precision" OR family STREQUAL "lunar")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SOURCE_ROOT}/third_party/sxtwl/LICENSE"
      "${input}/licenses/sxtwl-BSD-3-Clause.txt")
  endif()
  if(family STREQUAL "calendar" OR family STREQUAL "lunar" OR family STREQUAL "weather" OR family STREQUAL "air" OR family STREQUAL "input" OR family STREQUAL "album" OR family STREQUAL "player" OR family STREQUAL "timer")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/libraries/cyberglass/resources/lucide/LICENSE.txt" "${input}/licenses/Lucide-ISC.txt")
  endif()
  if(family STREQUAL "weather")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/nlohmann-json-MIT.txt"
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/weather-suite/licenses/Open-Meteo-Attribution.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "air")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/air-quality-suite/licenses/nlohmann-json-MIT.txt"
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/air-quality-suite/licenses/WAQI-Attribution.txt" "${input}/licenses")
  endif()
  if(family STREQUAL "productivity" OR family STREQUAL "disk" OR family STREQUAL "timer" OR family STREQUAL "countdown")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CD_PLUGIN_SOURCE_ROOT}/plugins/productivity-suite/licenses" "${input}/licenses")
  endif()
  if(folder STREQUAL "weather-4x2")
    list(APPEND art_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.png")
    list(APPEND extra COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/weather-scene/city.provenance.json" "${input}/assets")
  endif()
  cd_asset_target(${target}_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${input}/assets" "${input}/licenses"
      "${CMAKE_BINARY_DIR}/pixel-desk-packages"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target}>" "${input}/plugin.dll"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> descriptor "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto" "${input}/descriptor.pb"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/preview.png" "${input}/preview.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "${input}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/assets/NOTICE.txt" "${input}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_SDK_LICENSE}" "${input}/licenses/CyberDesktopSDK-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_TWEENY_LICENSE}"
      "${input}/licenses/Tweeny-MIT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CD_PLUGIN_YOGA_LICENSE}" "${input}/licenses/Yoga-MIT.txt"
    ${extra}
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${input}" "$<TARGET_FILE_DIR:${target}>"
    COMMAND $<TARGET_FILE:cd_plugin_package_tool> pack "${input}" "${CMAKE_BINARY_DIR}/pixel-desk-packages/${id}.cdpkg"
    DEPENDS ${target} cd_plugin_package_tool "${CMAKE_CURRENT_SOURCE_DIR}/packages/${folder}/plugin.textproto"
      ${art_dependencies} VERBATIM)
  list(APPEND packages ${target}_package)
endforeach()
add_custom_target(cd_pixel_desk_packages DEPENDS ${packages})


# Shared case artwork is independent of any individual widget model.
add_library(cd_pixel_desk_artwork STATIC PixelParts.cpp PixelLayout.cpp PixelImages.cpp PixelDigits.cpp PixelPanels.cpp PixelCase.cpp)
target_link_libraries(cd_pixel_desk_artwork PUBLIC CyberDesktopSDK::SDK)
target_include_directories(cd_pixel_desk_artwork PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_compile_options(cd_pixel_desk_artwork PRIVATE /utf-8 /W4)
set_target_properties(cd_pixel_desk_artwork PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
foreach(target cd_pixel_desk_core cd_pixel_desk_spring_core cd_pixel_desk_marble_core cd_pixel_desk_precision_core cd_pixel_desk_calendar_core cd_pixel_desk_lunar_core cd_pixel_desk_weather_core cd_pixel_desk_air_core cd_pixel_desk_monitor_core cd_pixel_desk_productivity_core cd_pixel_desk_input_core cd_pixel_desk_album_core cd_pixel_desk_player_core cd_pixel_desk_matrix_core cd_pixel_desk_beat_core cd_pixel_desk_toys_core cd_pixel_desk_chime_core cd_pixel_desk_sign_core cd_pixel_desk_hourglass_core cd_pixel_desk_basic_clock_core cd_pixel_desk_brick_core cd_pixel_desk_pond_core cd_pixel_desk_ambience_core)
  target_link_libraries(${target} PUBLIC cd_pixel_desk_artwork)
endforeach()

target_sources(cd_pixel_desk_weather_core PRIVATE WeatherArt.cpp)
target_sources(cd_pixel_desk_sign_core PRIVATE SignGlyphs.cpp)
target_sources(cd_pixel_desk_chime_core PRIVATE PixelChime.cpp)
target_sources(cd_pixel_desk_hourglass_core PRIVATE PixelHourglass.cpp)
target_sources(cd_pixel_desk_beat_core PRIVATE PixelBeat.cpp BeatArtwork.cpp)
