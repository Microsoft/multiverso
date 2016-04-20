#ifndef MULTIVERSO_UTIL_QUANTIZATION_UTIL_H_
#define MULTIVERSO_UTIL_QUANTIZATION_UTIL_H_

#include <multiverso/blob.h>
#include <vector>
#include <cmath>

namespace multiverso {
  class QuantizationFilter {
  public:
    QuantizationFilter() {}

    virtual ~QuantizationFilter() {}

    virtual void FilterIn(const std::vector<Blob>& blobs,
      std::vector<Blob>* outputs) = 0;

    virtual void FilterOut(const std::vector<Blob>& blobs,
      std::vector<Blob>* outputs) = 0;
  private:
  };

  template<typename data_type, typename index_type>
  class SparseFilter : public QuantizationFilter {
  public:
    explicit SparseFilter(double clip) :clip_value(clip) {}

    ~SparseFilter() {}

    // Returns compressed blobs given input blobs.
    // Each input blob in vector will generate two blobs in result:
    //  the first blob contains info: compressed or not, original
    //  blob size in byte; the second blob contains info: compressed
    //  blob if it's compressed or original blob.
    void FilterIn(const std::vector<Blob>& blobs,
      std::vector<Blob>* outputs) override {
      CHECK_NOTNULL(outputs);
      outputs->clear();
      Blob size_blob(sizeof(index_type) * blobs.size());
      outputs->push_back(size_blob);
      for (auto i = 0; i < blobs.size(); ++i) {
        auto& blob = blobs[i];
        Blob compressed_blob;
        auto compressed = TryCompress(blob, &compressed_blob);
        // size info (compressed ? size : -1)
#pragma warning( push )
#pragma warning( disable : 4267)
        size_blob.As<index_type>(i) = compressed ? blob.size() : -1;
#pragma warning( pop ) 
        outputs->push_back(compressed ? std::move(compressed_blob) : blob);
      }
    }

    // Returns de-compressed blobs from input
    //  blobs compressed by function FilterIn.
    void FilterOut(const std::vector<Blob>& blobs,
      std::vector<Blob>* outputs) override {
      CHECK_NOTNULL(outputs);
      CHECK(blobs.size() > 0);
      outputs->clear();
      auto& size_blob = blobs[0];
      for (auto i = 1; i < blobs.size(); i++) {
        // size info (compressed ? size : -1)
        auto is_compressed = size_blob.As<int>(i - 1) >= 0;
        auto size = is_compressed ?
          size_blob.As<int>(i - 1) : blobs[i].size();
        auto& blob = blobs[i];
        outputs->push_back(is_compressed ?
          std::move(DeCompress(blob, size)) : blob);
      }
    }

protected:

  bool TryCompress(const Blob& in_blob,
    Blob* out_blob) {
    CHECK_NOTNULL(out_blob);
#pragma warning( push )
#pragma warning( disable : 4127)
    CHECK(sizeof(data_type) == sizeof(index_type));
#pragma warning( pop ) 
    auto data_count = in_blob.size<data_type>();
    auto non_zero_count = 0;
    for (auto i = 0; i < data_count; ++i) {
      if (std::abs(in_blob.As<data_type>(i)) > clip_value) {
        ++non_zero_count;
      }
    }

    if (non_zero_count * 2 >= data_count)
      return false;

    if (non_zero_count == 0) {
      // Blob does not support empty content,
      //  fill the blob with first value

      Blob result(2 * sizeof(data_type));
      // set index
      result.As<index_type>(0) = 0;
      // set value
      result.As<data_type>(1) = in_blob.As<data_type>(0);
      *out_blob = result;
    }
    else {
      Blob result(non_zero_count * 2 * sizeof(data_type));
      auto result_index = 0;
      for (auto i = 0; i < data_count; ++i) {
        auto abs_value = std::abs(in_blob.As<data_type>(i));
        if (abs_value > clip_value) {
          // set index
          result.As<index_type>(result_index++) = i;
          // set value
          result.As<data_type>(result_index++) =
            in_blob.As<data_type>(i);
        }
      }
      CHECK(result_index == non_zero_count * 2);
      *out_blob = result;
    }

    return true;
  }

  Blob DeCompress(const Blob& in_blob, size_t size) {
#pragma warning( push )
#pragma warning( disable : 4127)
    CHECK(sizeof(data_type) == sizeof(index_type));
#pragma warning( pop ) 
    CHECK(size % sizeof(data_type) == 0);
    auto original_data_count = size / sizeof(data_type);
    Blob result(size);
    for (auto i = 0; i < original_data_count; ++i) {
      result.As<data_type>(i) = 0;
    }
    auto data_count = in_blob.size<data_type>();
    for (auto i = 0; i < data_count; i += 2) {
      auto index = in_blob.As<index_type>(i);
      auto value = in_blob.As<data_type>(i + 1);
      result.As<data_type>(index) = value;
    }

    return result;
  }
  private:
    double clip_value;
  };

  class OneBitsFilter : public QuantizationFilter{
  };
}  // namespace multiverso

#endif  // MULTIVERSO_UTIL_QUANTIZATION_UTIL_H_