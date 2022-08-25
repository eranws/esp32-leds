#ifndef __STEPS_FLOAT_FUNC_H__
#define __STEPS_FLOAT_FUNC_H__

#include <float_func/i_float_func.h>

#include <math.h>

class StepsFloatFunc : public IFloatFunc
{

public:
    void InitFromJson(const JsonObject &func_params)
    {
        numSteps = func_params["num"];
        valueDiff = func_params["diff"];
        firstStepValue = func_params["init"];
    }

    float GetValue(float relTime)
    {
        int currStep = (int)(relTime * numSteps);
        return firstStepValue + valueDiff * currStep;
    }

private:
    int numSteps;
    float valueDiff;
    float firstStepValue;
};

#endif // __STEPS_FLOAT_FUNC_H__