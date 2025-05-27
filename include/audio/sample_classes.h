#include <cstdint>

class BaseSampleData {
    public:
        BaseSampleData() = default;
        virtual int16_t get_sample(uint32_t index) = 0;
        virtual uint16_t size() = 0;
};

class SampleDataArray : public BaseSampleData {
    private:
        const int16_t *sample_array;
        uint16_t sample_size;

    public:
        SampleDataArray(const int16_t *array, uint16_t size) : sample_array(array), sample_size(size) {}
        
        inline virtual int16_t get_sample(uint32_t index) override {
            if (index < sample_size) {
                return sample_array[index];
            }
            return 0; // or handle out of bounds
        }

        inline virtual uint16_t size() override {
            return sample_size;
        }
};