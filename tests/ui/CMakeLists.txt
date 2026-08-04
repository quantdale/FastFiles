add_executable(fftreemap_layout_tests test_treemap_layout.cpp)
target_include_directories(fftreemap_layout_tests PRIVATE ../../src/ui/include)
add_test(NAME fftreemap_layout_tests COMMAND fftreemap_layout_tests)

# Modernize-ui-appearance foundation seams: token/color conversions, animation
# easing, and icon-cache keying are plain C++ testable without a renderer.
# UiAnimation.cpp is compiled in for FloatAnimation/SystemAnimationsEnabled.
add_executable(ffui_style_tests test_ui_style.cpp ../../src/ui/src/UiAnimation.cpp)
target_include_directories(ffui_style_tests PRIVATE ../../src/ui/include ../../src/ui/src)
target_link_libraries(ffui_style_tests PRIVATE d2d1 dwrite windowscodecs)
add_test(NAME ffui_style_tests COMMAND ffui_style_tests)
