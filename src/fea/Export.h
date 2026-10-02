#pragma once
// Result export for external post-processors (ParaView, VisIt, ...).

#include "fea/FeaTypes.h"

#include <string>

namespace cf::fea {

/// Writes the volume mesh as a VTK XML unstructured grid (.vtu, ASCII).
/// Point data: B-Rep face id of boundary nodes; with `stat`: displacement,
/// von Mises and the stress tensor; with `modal`: one mode shape vector per mode.
/// Throws FeaError if the file cannot be written.
void writeVtu(const std::string& path, const VolumeMesh& mesh, const StaticResult* stat = nullptr,
              const ModalResult* modal = nullptr);

} // namespace cf::fea
