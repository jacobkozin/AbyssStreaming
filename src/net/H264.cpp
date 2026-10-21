#include "net/H264.hpp"

namespace abyss::net {

namespace {

// Reads Exp-Golomb coded fields from an RBSP (escape bytes already removed).
class BitReader {
public:
    explicit BitReader(const Bytes &data) : data_(data) {}

    bool ok() const { return !overrun_; }

    std::uint32_t bits(int count)
    {
        std::uint32_t value = 0;
        for (int i = 0; i < count; ++i) {
            if (bit_ >= data_.size() * 8) {
                overrun_ = true;
                return 0;
            }
            value = (value << 1) | ((data_[bit_ / 8] >> (7 - bit_ % 8)) & 1u);
            ++bit_;
        }
        return value;
    }

    std::uint32_t ue()
    {
        int zeros = 0;
        while (bits(1) == 0 && ok()) {
            if (++zeros > 31) {
                overrun_ = true;
                return 0;
            }
        }
        return (1u << zeros) - 1 + bits(zeros);
    }

    std::int32_t se()
    {
        const std::uint32_t value = ue();
        return (value & 1) ? static_cast<std::int32_t>((value + 1) / 2) : -static_cast<std::int32_t>(value / 2);
    }

private:
    const Bytes &data_;
    size_t bit_{0};
    bool overrun_{false};
};

void skipScalingList(BitReader &reader, int size)
{
    int last = 8;
    int next = 8;
    for (int i = 0; i < size; ++i) {
        if (next != 0)
            next = (last + reader.se() + 256) % 256;
        last = next == 0 ? last : next;
    }
}

} // namespace

std::vector<ByteView> splitAnnexB(ByteView data)
{
    std::vector<ByteView> units;
    const size_t size = data.size();
    bool inUnit = false;
    size_t start = 0;
    size_t i = 0;
    while (i + 2 < size) {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            if (inUnit) {
                size_t end = i;
                // Drops the leading zero of a 4-byte start code (and any
                // trailing padding zeros, which decoders ignore).
                while (end > start && data[end - 1] == 0)
                    --end;
                if (end > start)
                    units.push_back(data.subspan(start, end - start));
            }
            i += 3;
            start = i;
            inUnit = true;
        } else {
            ++i;
        }
    }
    if (inUnit && start < size)
        units.push_back(data.subspan(start));
    return units;
}

Bytes annexBToAvcc(ByteView annexB)
{
    Bytes out;
    out.reserve(annexB.size() + 16);
    for (const ByteView nal : splitAnnexB(annexB)) {
        if (nalType(nal) == NalType::AccessUnitDelimiter)
            continue;
        putU32(out, static_cast<std::uint32_t>(nal.size()));
        putBytes(out, nal);
    }
    return out;
}

bool containsIdr(ByteView annexB)
{
    for (const ByteView nal : splitAnnexB(annexB)) {
        if (nalType(nal) == NalType::IdrSlice)
            return true;
    }
    return false;
}

std::optional<AvcParameterSets> findParameterSets(ByteView annexB)
{
    AvcParameterSets sets;
    for (const ByteView nal : splitAnnexB(annexB)) {
        if (nalType(nal) == NalType::Sps && sets.sps.empty())
            sets.sps.assign(nal.begin(), nal.end());
        else if (nalType(nal) == NalType::Pps && sets.pps.empty())
            sets.pps.assign(nal.begin(), nal.end());
    }
    if (sets.sps.size() < 4 || sets.pps.empty())
        return std::nullopt;
    return sets;
}

Bytes buildAvcDecoderConfig(const AvcParameterSets &sets)
{
    Bytes out;
    putU8(out, 1);           // configurationVersion
    putU8(out, sets.sps[1]); // AVCProfileIndication
    putU8(out, sets.sps[2]); // profile_compatibility
    putU8(out, sets.sps[3]); // AVCLevelIndication
    putU8(out, 0xFF);        // 6 reserved bits + lengthSizeMinusOne = 3 (4-byte lengths)
    putU8(out, 0xE1);        // 3 reserved bits + one SPS
    putU16(out, static_cast<std::uint16_t>(sets.sps.size()));
    putBytes(out, view(sets.sps));
    putU8(out, 1); // one PPS
    putU16(out, static_cast<std::uint16_t>(sets.pps.size()));
    putBytes(out, view(sets.pps));
    return out;
}

std::optional<SpsInfo> parseSps(ByteView sps)
{
    if (sps.size() < 4 || nalType(sps) != NalType::Sps)
        return std::nullopt;

    // Remove emulation-prevention bytes (00 00 03 -> 00 00).
    Bytes rbsp;
    int zeros = 0;
    for (size_t i = 1; i < sps.size(); ++i) {
        if (zeros >= 2 && sps[i] == 3) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(sps[i]);
        zeros = sps[i] == 0 ? zeros + 1 : 0;
    }

    BitReader r(rbsp);
    SpsInfo info;
    info.profileIdc = static_cast<int>(r.bits(8));
    r.bits(8); // constraint flags
    info.levelIdc = static_cast<int>(r.bits(8));
    r.ue(); // seq_parameter_set_id

    std::uint32_t chromaFormat = 1;
    switch (info.profileIdc) {
    case 100: case 110: case 122: case 244: case 44: case 83: case 86: case 118: case 128: case 138: case 139: case 134: case 135:
        chromaFormat = r.ue();
        if (chromaFormat == 3)
            r.bits(1); // separate_colour_plane_flag
        r.ue();        // bit_depth_luma_minus8
        r.ue();        // bit_depth_chroma_minus8
        r.bits(1);     // qpprime_y_zero_transform_bypass_flag
        if (r.bits(1)) { // seq_scaling_matrix_present_flag
            for (int i = 0; i < (chromaFormat == 3 ? 12 : 8); ++i) {
                if (r.bits(1))
                    skipScalingList(r, i < 6 ? 16 : 64);
            }
        }
        break;
    default: break;
    }

    r.ue(); // log2_max_frame_num_minus4
    const std::uint32_t pocType = r.ue();
    if (pocType == 0) {
        r.ue();
    } else if (pocType == 1) {
        r.bits(1);
        r.se();
        r.se();
        const std::uint32_t cycle = r.ue();
        for (std::uint32_t i = 0; i < cycle && r.ok(); ++i)
            r.se();
    }
    r.ue();    // max_num_ref_frames
    r.bits(1); // gaps_in_frame_num_value_allowed_flag
    const std::uint32_t widthMbs = r.ue() + 1;
    const std::uint32_t heightUnits = r.ue() + 1;
    const std::uint32_t frameMbsOnly = r.bits(1);
    if (!frameMbsOnly)
        r.bits(1); // mb_adaptive_frame_field_flag
    r.bits(1);     // direct_8x8_inference_flag

    std::uint32_t cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
    if (r.bits(1)) {
        cropLeft = r.ue();
        cropRight = r.ue();
        cropTop = r.ue();
        cropBottom = r.ue();
    }
    if (!r.ok())
        return std::nullopt;

    const std::uint32_t cropUnitX = (chromaFormat == 1 || chromaFormat == 2) ? 2 : 1;
    const std::uint32_t cropUnitY = (chromaFormat == 1 ? 2 : 1) * (2 - frameMbsOnly);
    info.width = static_cast<int>(widthMbs * 16 - cropUnitX * (cropLeft + cropRight));
    info.height = static_cast<int>((2 - frameMbsOnly) * heightUnits * 16 - cropUnitY * (cropTop + cropBottom));
    return info;
}

} // namespace abyss::net
