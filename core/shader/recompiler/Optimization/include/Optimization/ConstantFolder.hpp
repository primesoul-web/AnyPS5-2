#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_CONSTANTFOLDER_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_CONSTANTFOLDER_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include "IntermediateRepresentation/IrValue.hpp"

#include <span>

namespace ShaderRecompiler {

class ConstantFolder {
public:
    explicit constexpr ConstantFolder(std::uint32_t vertexHostSubgroupSize = 0) : vertexHostSubgroupSize(vertexHostSubgroupSize) {}

    void Fold(IrProgram& program) const;

    void Fold(IrProgram& program, std::span<IrBlock* const> blocks) const;

    [[nodiscard]] bool ComposePackedAncillary(IrProgram& program) const;

private:
    std::uint32_t vertexHostSubgroupSize;

    [[nodiscard]] bool tryFoldValue(IrProgram& program, IrValue& value) const;
};

}

#endif
