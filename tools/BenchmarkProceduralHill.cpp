// Build with the target compiler's optimized, precise floating-point mode.
#include "../integrations/o3de/Code/Include/TerrainCompositor/ProceduralHillKernel.h"
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <tuple>
#include <vector>
#if defined(_M_X64) || defined(__SSE__)
#include <xmmintrin.h>
#define HILL_SSE 1
#endif

using namespace TerrainCompositor;
using Clock = std::chrono::steady_clock;
struct Point { float x, y; float GetX() const { return x; } float GetY() const { return y; } };
volatile float checksum = 0;
volatile float setupFrequency = 3;
#ifdef _MSC_VER
__declspec(noinline)
#endif
ProceduralHillKernel ConstructKernel(float f, ProceduralHillPolicy p) { return { 0.0099f, 3, f, p }; }
template<class F> double Time(F&& f, int repeats = 7)
{
    std::vector<double> times;
    for (int i = 0; i < repeats; ++i)
    {
        const auto start = Clock::now(); f();
        times.push_back(std::chrono::duration<double, std::nano>(Clock::now() - start).count());
    }
    std::sort(times.begin(), times.end()); return times[times.size() / 2];
}
double PreciseProfile(double q, double f)
{
    const double p = std::clamp(1.0 - std::sqrt(q), 0.0, 1.0);
    return std::pow(p * p * (3.0 - 2.0 * p), f);
}
float Profile(float q, float f) { return std::pow(ProceduralHillKernel::SmoothCurve(1.0f - std::sqrt(q)), f); }
float ApproxSqrt(float x, int refinements)
{
    if (!(x > 0) || !std::isfinite(x)) return std::sqrt(x);
#ifdef HILL_SSE
    float r = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    if (refinements) r = r * (1.5f - 0.5f * x * r * r);
    return x * r;
#else
    (void)refinements; return std::sqrt(x);
#endif
}
struct Error
{
    double max = 0, squared = 0, worstX = 0; size_t n = 0, changed = 0;
    void Add(double a, double b, double x)
    {
        const double e = std::abs(a - b); if (e > max) { max = e; worstX = x; }
        squared += e * e; ++n; changed += a != b;
    }
    double Rms() const { return std::sqrt(squared / double(n)); }
};
void Kernels()
{
    std::cout << "kind,workload,frequency,policy,samples,median_ns_per_sample,cold_ns_per_sample,setup_ns,cells,cell_hits,sqrt,pow,scratch_bytes,allocations\n";
    for (const auto [name, spacing, count] : { std::tuple{"dense", 0.5f, 131}, {"coarse", 32.0f, 67}, {"sparse", 997.13f, 17}, {"tiny", 0.5f, 2} })
        for (float frequency : { 0.1f, 1.5f, 3.0f, 16.0f })
            for (auto policy : { ProceduralHillPolicy::Reference, ProceduralHillPolicy::PrunedReference,
                     ProceduralHillPolicy::CachedExact, ProceduralHillPolicy::IntegerPowers })
            {
                std::vector<Point> points;
                for (int y = 0; y < count; ++y) for (int x = 0; x < count; ++x)
                    points.push_back({ -64.125f + float(x) * spacing, -17.375f + float(y) * spacing });
                std::vector<float> values(points.size());
                setupFrequency = frequency;
                const double setup = Time([&] { for (int i = 0; i < 10000; ++i) { const auto k = ConstructKernel(setupFrequency, policy); checksum = k.Frequency(); } }) / 10000;
                const ProceduralHillKernel kernel(0.0099f, 3, frequency, policy);
                const auto run = [&] { kernel.SampleBatch(points, std::span(values)); checksum = values[values.size() / 2]; };
                const double cold = Time(run, 1) / double(points.size());
                const double warm = Time(run, 11) / double(points.size());
                ProceduralHillCounters c; kernel.SampleBatch(points, std::span(values), &c);
                std::cout << "kernel," << name << ',' << frequency << ',' << int(policy) << ',' << points.size() << ',' << warm << ',' << cold << ',' << setup
                    << ',' << c.m_cells << ',' << c.m_cellHits << ',' << c.m_sqrt << ',' << c.m_pow << ',' << sizeof(ProceduralHillKernel::Scratch) << ",0\n";
            }
}
void Profiles()
{
    std::cout << "kind,frequency,variant,table_bytes,setup_ns,median_ns,max_height_m,rms_height_m,worst_q,high_precision_max_m,high_precision_rms_m\n";
    std::vector<float> points;
    for (int i = 0; i <= 200000; ++i) points.push_back(float(i) / 200000.0f);
    float edge = 1;
    for (int i = 0; i < 32768; ++i) { points.push_back(edge); edge = std::nextafter(edge, 0.0f); }
    for (float frequency : { 0.1f, 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 7.3f, 16.0f })
        for (int variant : { 0, 1, 2, 1024, 4096, 16384 })
        {
            std::vector<float> table;
            const auto setupStart = Clock::now();
            if (variant >= 1024)
            {
                table.resize(size_t(variant) + 1);
                for (int i = 0; i <= variant; ++i) table[i] = Profile(float(i) / float(variant), frequency);
                table.front() = 1; table.back() = 0;
            }
            const double setup = std::chrono::duration<double, std::nano>(Clock::now() - setupStart).count();
            const auto eval = [&](float q)
            {
                if (q <= 0) return 1.0f;
                if (q >= 1) return 0.0f;
                if (variant == 0) return Profile(q, frequency);
                if (variant < 1024) return std::pow(ProceduralHillKernel::SmoothCurve(1 - ApproxSqrt(q, variant - 1)), frequency);
                const float index = q * float(variant);
                const size_t i = std::min(size_t(index), table.size() - 2);
                const float t = index - float(i);
                return table[i] + t * (table[i + 1] - table[i]);
            };
            std::vector<float> values(points.size());
            const double ns = Time([&] { for (size_t i = 0; i < points.size(); ++i) values[i] = eval(points[i]); checksum = values[99]; }) / double(points.size());
            Error vsFloat, vsDouble;
            for (size_t i = 0; i < points.size(); ++i)
            {
                vsFloat.Add(values[i] * 1024.0, Profile(points[i], frequency) * 1024.0, points[i]);
                vsDouble.Add(values[i] * 1024.0, PreciseProfile(points[i], frequency) * 1024.0, points[i]);
            }
            std::cout << "profile," << frequency << ',' << variant << ',' << table.size() * sizeof(float) << ',' << setup << ',' << ns << ','
                << vsFloat.max << ',' << vsFloat.Rms() << ',' << vsFloat.worstX << ',' << vsDouble.max << ',' << vsDouble.Rms() << '\n';
        }
}
void SqrtVectors()
{
#ifdef HILL_SSE
    std::vector<float> input(262144), output(input.size());
    for (size_t i = 0; i < input.size(); ++i) input[i] = 0.00001f + float(i) / float(input.size());
    for (int variant : { 0, 1, 2 })
    {
        const double ns = Time([&]
        {
            for (size_t i = 0; i < input.size(); i += 4)
            {
                const auto x = _mm_loadu_ps(input.data() + i);
                auto y = _mm_sqrt_ps(x);
                if (variant)
                {
                    auto r = _mm_rsqrt_ps(x);
                    if (variant == 2) r = _mm_mul_ps(r, _mm_sub_ps(_mm_set1_ps(1.5f),
                        _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_set1_ps(0.5f), x), r), r)));
                    y = _mm_mul_ps(x, r);
                }
                _mm_storeu_ps(output.data() + i, y);
            }
            checksum = output[100];
        }) / double(input.size());
        Error error;
        for (size_t i = 0; i < input.size(); ++i) error.Add(output[i], std::sqrt(double(input[i])), input[i]);
        std::cout << "vector_sqrt," << variant << ',' << ns << ',' << error.max << ',' << error.Rms() << '\n';
    }
#endif
}
double PreciseHeight(float x, float y, float density, float amplitude, float frequency)
{
    // Same captured float configuration, scaled coordinates and cell centers;
    // evaluate radial math, unions and normalization in double precision.
    const float cx = x * density, cy = y * density;
    const auto bx = int64_t(std::floor(cx)), by = int64_t(std::floor(cy));
    double bumps = 0, depressions = 0;
    for (int64_t oy = -1; oy <= 1; ++oy) for (int64_t ox = -1; ox <= 1; ++ox)
    {
        const auto cell = ProceduralHillKernel::MakeCell(bx + ox, by + oy);
        const double dx = double(cx) - cell.m_x, dy = double(cy) - cell.m_y;
        const double p = std::clamp(1 - std::sqrt(dx * dx + dy * dy) / double(ProceduralHillKernel::Radius), 0.0, 1.0);
        const double h = std::pow(p * p * (3 - 2 * p), double(frequency));
        double& blend = cell.m_depression ? depressions : bumps;
        blend += (1 - blend) * h;
    }
    return (bumps - depressions) * double(amplitude);
}
void Numerics()
{
    std::cout << "kind,density,amplitude,frequency,samples,exact_changed,height_max_m,height_rms_m,normal_max_degrees,normal_rms_degrees,packed_height_max_units,packed_normal_max_units,packed_height_changed,packed_normal_changed,high_precision_max_m,high_precision_rms_m,reference_high_precision_max_m,worst_x\n";
    size_t exactFailures = 0;
    for (float density : { 0.000001f, 0.0099f, 0.3f, 1.0f })
        for (float amplitude : { 3.0f, 180.0f, 1024.0f })
            for (float frequency : { 0.1f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 7.3f, 16.0f })
            {
                const ProceduralHillKernel reference(density, amplitude, frequency, ProceduralHillPolicy::Reference);
                const ProceduralHillKernel exact(density, amplitude, frequency), approx(density, amplitude, frequency, ProceduralHillPolicy::IntegerPowers);
                ProceduralHillKernel::Scratch a, b, c;
                Error height, normal, packedHeight, packedNormal, precise, preciseReference;
                size_t exactChanged = 0;
                for (int i = 0; i < 8192; ++i)
                {
                    const uint64_t bits = ProceduralHillKernel::MixBits(uint64_t(i));
                    float x = float(int(bits & 65535) - 32768) * 0.0173f;
                    float y = float(int((bits >> 16) & 65535) - 32768) * 0.0379f;
                    if (i < 4096)
                    {
                        const auto center = ProceduralHillKernel::MakeCell(i % 13 - 6, i % 17 - 8);
                        // Dense support rims, centers, negative cells and fractional
                        // positions, using the same point in every kernel variant.
                        x = (center.m_x + (i % 31 == 0 ? 0 : ProceduralHillKernel::Radius + float(i % 257 - 128) * 1e-7f)) / density;
                        y = center.m_y / density;
                    }
                    const float ref = reference.Sample(x, y, a), ex = exact.Sample(x, y, b), ap = approx.Sample(x, y, c);
                    exactChanged += std::memcmp(&ref, &ex, sizeof(float)) != 0;
                    height.Add(ap * 2048.0 - 1024.0, ref * 2048.0 - 1024.0, x);
                    const double highPrecision = PreciseHeight(x, y, density, amplitude, frequency);
                    precise.Add(ap * 2048.0 - 1024.0, highPrecision, x);
                    preciseReference.Add(ref * 2048.0 - 1024.0, highPrecision, x);
                    const auto packHeight = [](float h) { return int(h * 32767.0f + 0.5f) * 2; };
                    packedHeight.Add(packHeight(ap), packHeight(ref), x);
                    const auto n = [&](const ProceduralHillKernel& kernel, ProceduralHillKernel::Scratch& scratch)
                    {
                        const float sx = (kernel.Sample(x - 0.5f, y, scratch) - kernel.Sample(x + 0.5f, y, scratch)) * 2048.0f;
                        const float sy = (kernel.Sample(x, y - 0.5f, scratch) - kernel.Sample(x, y + 0.5f, scratch)) * 2048.0f;
                        const double inv = 1 / std::sqrt(double(sx) * sx + double(sy) * sy + 1);
                        return std::array<double, 3>{ sx * inv, sy * inv, inv };
                    };
                    const auto nr = n(reference, a), na = n(approx, c);
                    const double distance = std::sqrt((nr[0]-na[0])*(nr[0]-na[0]) + (nr[1]-na[1])*(nr[1]-na[1]) + (nr[2]-na[2])*(nr[2]-na[2]));
                    normal.Add(2 * std::asin(std::min(1.0, distance / 2)) * 180 / 3.141592653589793, 0, x);
                    for (int axis = 0; axis < 2; ++axis) packedNormal.Add(std::lround(na[axis] * 127), std::lround(nr[axis] * 127), x);
                }
                exactFailures += exactChanged;
                std::cout << "numerics," << density << ',' << amplitude << ',' << frequency << ',' << height.n << ',' << exactChanged << ','
                    << height.max << ',' << height.Rms() << ',' << normal.max << ',' << normal.Rms() << ',' << packedHeight.max << ',' << packedNormal.max << ','
                    << packedHeight.changed << ',' << packedNormal.changed << ',' << precise.max << ',' << precise.Rms() << ',' << preciseReference.max << ',' << height.worstX << '\n';
                // Incremental experimental budgets at the tested 0.5m normal
                // spacing. These are measured limits, not a global error proof.
                if (height.max > 0.001 || normal.max > 0.1 || packedHeight.max > 2 || packedNormal.max > 1) std::exit(3);
            }
    if (exactFailures) std::exit(2);
}
int main(int argc, char** argv)
{
    std::cout << std::setprecision(10);
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "all" || mode == "kernels") Kernels();
    if (mode == "all" || mode == "profiles") Profiles();
    if (mode == "all" || mode == "sqrt") SqrtVectors();
    if (mode == "all" || mode == "numerics") Numerics();
}
