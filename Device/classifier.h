//
// ELL header for module model
// Compiled from input model classifier.ell
//

#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C"
{
#endif // defined(__cplusplus)

//
// Types
//

#if !defined(ELL_TensorShape)
#define ELL_TensorShape

typedef struct TensorShape
{
    int32_t rows;
    int32_t columns;
    int32_t channels;
} TensorShape;

#endif // !defined(ELL_TensorShape)

//
// Functions
//

// Input 0 ('32') size: 80
// Output 0 ('output') size: 31
void model_Predict(void* context, float* input, float* output);

void model_Reset();

int32_t model_GetInputSize(int32_t index);

int32_t model_GetOutputSize(int32_t index);

int32_t model_GetSinkOutputSize(int32_t index);

int32_t model_GetNumNodes();

void model_GetInputShape(int32_t index, TensorShape* shape);

void model_GetOutputShape(int32_t index, TensorShape* shape);

void model_GetSinkOutputShape(int32_t index, TensorShape* shape);

char* model_GetMetadata(char* key);

#if defined(__cplusplus)
} // extern "C"
#endif // defined(__cplusplus)


#if !defined(MODEL_WRAPPER_DEFINED)
#define MODEL_WRAPPER_DEFINED

#if defined(__cplusplus)

#include <cstring> // memcpy
#include <vector>

#ifndef HIGH_RESOLUTION_TIMER
#define HIGH_RESOLUTION_TIMER
// This is a default implementation of HighResolutionTimer using C++ <chrono> library.  The
// GetMilliseconds function is used in the steppable version of the Predict method.  
// If your platform requires a different implementation then define HIGH_RESOLUTION_TIMER 
// before including this header.
#include <chrono>

class HighResolutionTimer
{
public:
    void Reset()
    {
        _started = false;
    }

    /// <summary> Return high precision number of seconds since first call to Predict. </summary>
    double GetMilliseconds() 
    {    
        if (!_started)
        {
            _start = std::chrono::high_resolution_clock::now();
            _started = true;
        }
        auto now = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - _start);
        return static_cast<double>(us.count()) / 1000.0;
    }
private:
    std::chrono::high_resolution_clock::time_point _start;
    bool _started = false;
};
#endif

// This class wraps the "C" interface and provides handy virtual methods you can override to
// intercept any callbacks.  It also wraps the low level float buffers with std::vectors.
// This class can then be wrapped by SWIG which will give you an interface to work with from
// other languages (like Python) where you can easily override those virtual methods.
class ModelWrapper
{
public:
    ModelWrapper()
    {
        _predict_output.resize(GetOutputSize(0));

    }

    virtual ~ModelWrapper() = default;

    TensorShape GetInputShape(int index = 0) const
    {    
        TensorShape inputShape;
        model_GetInputShape(index, &inputShape);
        return inputShape;
    }

    int GetInputSize(int index = 0) const
    {
        return model_GetInputSize(index);
    }
    
    TensorShape GetOutputShape(int index = 0) const
    {
        TensorShape outputShape;
        model_GetOutputShape(index, &outputShape);
        return outputShape;
    }

    int GetOutputSize(int index = 0) const
    {
        return model_GetOutputSize(index);
    }
    
    void Reset()
    {
        model_Reset();

    }

    bool IsSteppable() const 
    {
        return false;
    }

    const char* GetMetadata(const char* name) const
    {
        return model_GetMetadata((char*)name);
    }

    TensorShape GetSinkShape(int index = 0) const
    {
        TensorShape inputShape;
        model_GetSinkOutputShape(index, &inputShape);
        return inputShape;
    }

    int GetSinkOutputSize(int index = 0) const
    {
        return model_GetSinkOutputSize(index);
    }

    void Internal_VadCallback(int32_t* buffer)
    {
        int32_t size = GetSinkOutputSize(0);
        _model_VadCallback_output.assign(buffer, buffer + size);
        VadCallback(_model_VadCallback_output);
    }

    virtual void VadCallback(std::vector<int32_t>& output)
    {
        // override this method to get the sink callback data as a vector
    }

    std::vector<float>& Predict(std::vector<float>& input)
    {
        model_Predict(this, input.data(), _predict_output.data());
        return _predict_output;
    }


private:
    std::vector<float> _predict_output;
    std::vector<int32_t> _model_VadCallback_output;
      
};


#if !defined(MODELWRAPPER_CDECLS)
#define MODELWRAPPER_CDECLS

extern "C"
{
    void model_VadCallback(void* context, int32_t* output)
    {
        if (context != nullptr)
        {
            auto predictor = reinterpret_cast<ModelWrapper*>(context);
            predictor->Internal_VadCallback(output);
        }
    }

}

#endif // !defined(MODELWRAPPER_CDECLS)
#endif // defined(__cplusplus)
#endif // !defined(MODEL_WRAPPER_DEFINED)
