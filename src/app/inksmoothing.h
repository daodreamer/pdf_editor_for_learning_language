#pragma once
#include "inkstroke.h"
#include <QPointF>
#include <algorithm>
#include <cmath>
#include <numbers>

// Pointer smoothing for hand-drawn ink, in logical pixels and event milliseconds.
// A One Euro filter (Casiez, Roussel & Vogel, CHI 2012): at low speed its low cutoff
// removes the mouse's integer-pixel staircase and hand tremor; the cutoff rises with
// speed, so fast handwriting keeps its shape with little lag. Speed also gives mouse
// ink a ballpoint-like width factor; a tablet's pressure is used instead when present.
class InkSmoother {
public:
    struct Sample { QPointF position; double factor; };
    double factor() const { return factor_; }
    void reset(QPointF p,double ms,double pressure) {
        position_=raw_=p; velocity_={}; ms_=ms;
        factor_=pressure>=0 ? pressureFactor(pressure) : 1.0;
    }
    Sample push(QPointF p,double ms,double pressure) {
        // Several moves may share a millisecond timestamp; never divide by zero.
        const double dtMs=std::max(ms-ms_,.5),dt=dtMs/1000;
        ms_=std::max(ms,ms_);
        const QPointF velocity=(p-raw_)/dt; raw_=p;
        velocity_+=(velocity-velocity_)*alpha(AnnotationConfig::smoothDerivativeCutoff,dt);
        const double speed=std::hypot(velocity_.x(),velocity_.y()); // px/s
        position_+=(p-position_)*alpha(AnnotationConfig::smoothMinCutoff+AnnotationConfig::smoothBeta*speed,dt);
        double target;
        if(pressure>=0) target=pressureFactor(pressure);
        else {
            using namespace AnnotationConfig;
            double t=std::clamp((speed/1000-inkSlowSpeed)/(inkFastSpeed-inkSlowSpeed),0.0,1.0);
            t=t*t*(3-2*t);
            target=inkSlowFactor+(inkFastFactor-inkSlowFactor)*t;
        }
        factor_+=(target-factor_)*(1-std::exp(-dtMs/AnnotationConfig::inkFactorResponseMs));
        return {position_,factor_};
    }
private:
    static double alpha(double cutoff,double dt) {
        const double tau=1/(2*std::numbers::pi*cutoff);
        return 1/(1+tau/dt);
    }
    static double pressureFactor(double pressure) { return .35+.85*std::clamp(pressure,0.0,1.0); }
    QPointF position_, raw_, velocity_;
    double ms_=0, factor_=1;
};
