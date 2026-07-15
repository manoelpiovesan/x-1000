#pragma once
#include "samples/Sample.hpp"
#include <array>
#include <memory>
#include <string>

namespace xpad::samples {

constexpr int kPadCount = 8;

struct SampleBank {
    std::string name{"Default"};
    std::array<std::shared_ptr<Sample>, kPadCount> pads{};

    [[nodiscard]] std::shared_ptr<Sample> pad(int index) const noexcept {
        if (index < 0 || index >= kPadCount) return nullptr;
        return pads[static_cast<std::size_t>(index)];
    }

    void setPad(int index, std::shared_ptr<Sample> sample) {
        if (index >= 0 && index < kPadCount) {
            pads[static_cast<std::size_t>(index)] = std::move(sample);
        }
    }
};

} // namespace xpad::samples

