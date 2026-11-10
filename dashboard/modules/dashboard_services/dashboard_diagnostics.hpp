// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace exbox_services::diagnostics {

inline constexpr std::size_t frame_capacity=240;
inline constexpr std::uint64_t gpu_budget_bytes=240ull*1024ull*1024ull;

struct Snapshot {
    bool enabled=false;
    std::size_t frame_samples=0;
    double p50_ms=0,p99_ms=0,max_ms=0;
    std::uint64_t frames_over_16_667=0;
    std::uint64_t uploads=0,upload_bytes=0,command_batches=0,fallbacks=0;
    std::uint64_t gpu_current_bytes=0,gpu_peak_bytes=0,missing_assets=0,unsupported_formats=0;
    bool uploads_published=false,gpu_memory_published=false,missing_assets_published=false,unsupported_formats_published=false;
    bool over_budget=false;
};

class Metrics {
public:
    bool enabled()const{return enabled_;}
    void set_enabled(bool value){enabled_=value;if(!value)clear_window();}
    void add_frame(double milliseconds){if(!enabled_||milliseconds<0||milliseconds>60000)return;frames_[next_]=milliseconds;next_=(next_+1)%frame_capacity;count_=std::min(count_+1,frame_capacity);if(milliseconds>16.667)sat_add(over_16_667_,1);}
    void add_upload(std::uint64_t bytes){if(enabled_){uploads_published_=true;sat_add(uploads_,1);sat_add(upload_bytes_,bytes);}}
    void add_command_batch(){if(enabled_)sat_add(command_batches_,1);}
    void add_fallback(){if(enabled_)sat_add(fallbacks_,1);}
    void set_gpu_bytes(std::uint64_t bytes){if(enabled_){gpu_memory_published_=true;gpu_current_bytes_=bytes;gpu_peak_bytes_=std::max(gpu_peak_bytes_,bytes);}}
    void add_missing_asset(){if(enabled_){missing_assets_published_=true;sat_add(missing_assets_,1);}}
    void add_unsupported_format(){if(enabled_){unsupported_formats_published_=true;sat_add(unsupported_formats_,1);}}
    Snapshot snapshot()const{
        Snapshot out;out.enabled=enabled_;out.frame_samples=count_;out.frames_over_16_667=over_16_667_;out.uploads=uploads_;out.upload_bytes=upload_bytes_;out.command_batches=command_batches_;out.fallbacks=fallbacks_;out.gpu_current_bytes=gpu_current_bytes_;out.gpu_peak_bytes=gpu_peak_bytes_;out.missing_assets=missing_assets_;out.unsupported_formats=unsupported_formats_;out.uploads_published=uploads_published_;out.gpu_memory_published=gpu_memory_published_;out.missing_assets_published=missing_assets_published_;out.unsupported_formats_published=unsupported_formats_published_;out.over_budget=gpu_memory_published_&&gpu_peak_bytes_>gpu_budget_bytes;
        if(!count_)return out;std::array<double,frame_capacity> sorted{};for(std::size_t i=0;i<count_;++i)sorted[i]=frames_[i];std::sort(sorted.begin(),sorted.begin()+std::ptrdiff_t(count_));
        const auto percentile=[&](double q){const std::size_t index=std::min(count_-1,std::size_t(q*double(count_-1)+0.999999));return sorted[index];};
        out.p50_ms=percentile(.50);out.p99_ms=percentile(.99);out.max_ms=sorted[count_-1];return out;
    }
private:
    static void sat_add(std::uint64_t&value,std::uint64_t amount){value=amount>UINT64_MAX-value?UINT64_MAX:value+amount;}
    void clear_window(){next_=count_=0;over_16_667_=uploads_=upload_bytes_=command_batches_=fallbacks_=gpu_current_bytes_=gpu_peak_bytes_=missing_assets_=unsupported_formats_=0;uploads_published_=gpu_memory_published_=missing_assets_published_=unsupported_formats_published_=false;}
    bool enabled_=false;std::array<double,frame_capacity>frames_{};std::size_t next_=0,count_=0;
    std::uint64_t over_16_667_=0,uploads_=0,upload_bytes_=0,command_batches_=0,fallbacks_=0,gpu_current_bytes_=0,gpu_peak_bytes_=0,missing_assets_=0,unsupported_formats_=0;
    bool uploads_published_=false,gpu_memory_published_=false,missing_assets_published_=false,unsupported_formats_published_=false;
};

} // namespace exbox_services::diagnostics
