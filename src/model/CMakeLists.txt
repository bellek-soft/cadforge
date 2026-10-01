# Parametric document model. Independent of rendering and UI, so it can be
# reused by tests, a future command-line batch tool or a scripting binding.
add_library(cadforge_model STATIC
    Property.cpp
    Feature.cpp
    FeatureRegistry.cpp
    Document.cpp
    History.cpp
    features/PartFeatures.cpp)
target_link_libraries(cadforge_model
    PUBLIC cadforge::core cadforge::geom nlohmann_json::nlohmann_json)
cadforge_set_warnings(cadforge_model)
add_library(cadforge::model ALIAS cadforge_model)
