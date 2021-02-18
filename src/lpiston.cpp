#include "box.h"
#include "energy.h"
#include "mdcalc.h"
#include "mdegv.h"
#include "mdintg.h"
#include "mdpq.h"
#include "mdpt.h"
#include "nose.h"
#include "random.h"
#include <cmath>
#include <tinker/detail/bath.hh>
#include <tinker/detail/inform.hh>
#include <tinker/detail/mdstuf.hh>
#include <tinker/detail/stodyn.hh>
#include <tinker/detail/units.hh>


namespace tinker {
namespace {
void lp(time_prec dt, double R)
{
   constexpr int D = 3;
   constexpr int nc = 1;

   // const time_prec h = w[j] * dt / (2 * nc);
   const time_prec h = dt / (2 * nc);
   const time_prec h_2 = h * 0.5;
   const double gh = stodyn::friction * h;
   const double gh_2 = stodyn::friction * h_2;
   const double kbt = units::gasconst * bath::kelvin;
   const double sd =
      std::sqrt((1.0 - std::exp(-2 * stodyn::friction * dt)) * kbt / qbar) /
      (2 * nc);


   const double odnf = 1.0 + D / mdstuf::nfree;


   // trace of virial: xx+yy+zz
   const virial_prec tr_vir = vir[0] + vir[4] + vir[8];
   T_prec temp;
   kinetic(temp);
   double eksum0 = eksum;   // new kinetic energy
   double vbox0 = volbox(); // new box volume
   double vscal0 = 1.0;     // scalar for velocity
   double lensc0 = 1.0;     // scalar for box length


   for (int k = 0; k < nc; ++k) {
      double eksum1 = eksum0;
      double vbox1 = vbox0;
      double vscal1 = vscal0;
      double lensc1 = lensc0;


      // units::prescon ~ 6.86*10^4
      // 1 kcal/mol/Ang**3 = prescon atm
      double DelP, DelP_m;
      double egh2;
      double vh, term;


      // v 1/2
      vh = vbar * h_2;
      term = std::exp(-odnf * vh);
      vscal1 *= term;
      eksum1 *= (term * term);


      // vbar and volume
      DelP = odnf * 2 * eksum1 - tr_vir;
      DelP = DelP - D * vbox1 * bath::atmsph / units::prescon;
      DelP_m = DelP / qbar;
      egh2 = std::exp(-gh_2);
      vbar = (1.0 - egh2) * DelP_m / stodyn::friction + egh2 * vbar + sd * R;
      vh = vbar * h;
      lensc1 *= std::exp(vh);
      vbox1 *= std::exp(D * vh);


      // v 2/2
      vh = vbar * h_2;
      term = std::exp(-odnf * vh);
      vscal1 *= term;
      eksum1 *= (term * term);


      // save
      eksum0 = eksum1;
      vbox0 = vbox1;
      vscal0 = vscal1;
      lensc0 = lensc1;
   }


   // scale atomic velocities
   darray::scale(g::q0, n, vscal0, vx);
   darray::scale(g::q0, n, vscal0, vy);
   darray::scale(g::q0, n, vscal0, vz);
   double vscal2 = vscal0 * vscal0;
   eksum *= vscal2;
   for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j)
         ekin[i][j] *= vscal2;


   // update the periodic box size
   lvec1 *= lensc0;
   lvec2 *= lensc0;
   lvec3 *= lensc0;
   set_default_recip_box();
}


void vv_lpiston_npt_v0(int istep, time_prec dt)
{
   int vers1 = rc_flag & calc::vmask;
   bool save = !(istep % inform::iwrite);
   if (!save)
      vers1 &= ~calc::energy;


   const double R = normal<double>(); // N(0,1)
   const time_prec dt_2 = 0.5 * dt;


   // iL1 1/2
   lp(dt, R);


   // iLf 1/2
   propagate_velocity(dt_2, gx, gy, gz);


   // iLr
   const double term = vbar * dt_2;
   const double term2 = term * term;
   const double expterm = std::exp(term);
   const double eterm2 = expterm * expterm;
   constexpr double e2 = 1.0 / 6;
   constexpr double e4 = 1.0 / 120;
   constexpr double e6 = 1.0 / 5040;
   constexpr double e8 = 1.0 / 362880;
   // sinh(x)/x: Taylor series
   double poly = 1 + term2 * (e2 + term2 * (e4 + term2 * (e6 + term2 * e8)));
   poly *= expterm * dt;
   propagate_pos_axbv(eterm2, poly);
   copy_pos_to_xyz(true);


   energy(vers1);


   // iLf 2/2
   propagate_velocity(dt_2, gx, gy, gz);


   // iL1 2/2
   lp(dt, R);
}


void lp_v5(time_prec dt, double R)
{
   const double D = 3.0;
   const double Nf = mdstuf::nfree;
   const double g = stodyn::friction;
   const double kbt = units::gasconst * bath::kelvin;
   const double odnf = 1.0 + D / Nf;


   const int nc = 2;
   const double h = dt / (2 * nc);
   const double h_2 = 0.5 * h;
   const double opgh2 = 1.0 + g * h_2;
   const double omgh2 = 1.0 - g * h_2;
   const double sdbar = std::sqrt(2.0 * kbt * g * dt / qbar) / (2 * nc);


   const double& qnh0 = qnh[0];
   double& gnh0 = gnh[0];
   double& vnh0 = vnh[0];


   const virial_prec tr_vir = vir[0] + vir[4] + vir[8];
   const double vol0 = volbox();
   double temp0;
   kinetic(temp0);


   double DelP;
   double eksum0 = eksum, eksum1;
   double velsc0 = 1.0, velsc1;
   for (int k = 0; k < nc; ++k) {
      eksum1 = eksum0;
      velsc1 = velsc0;


      // vnh 1/2
      gnh0 = (2 * eksum1 + qbar * vbar * vbar - Nf * kbt) / qnh0;
      vnh0 = vnh0 + gnh0 * h_2;


      // vbar 1/2
      DelP = (odnf * 2 * eksum1 - tr_vir);
      DelP = DelP - D * vol0 * bath::atmsph / units::prescon;
      gbar = DelP / qbar;
      vbar = omgh2 * vbar + gbar * h_2 + sdbar * R;


      // velocity
      double scal = std::exp(-h * (odnf * vbar + vnh0));
      velsc1 *= scal;
      eksum1 *= (scal * scal);


      // vbar 2/2
      DelP = (odnf * 2 * eksum1 - tr_vir);
      DelP = DelP - D * vol0 * bath::atmsph / units::prescon;
      gbar = DelP / qbar;
      vbar = (vbar + gbar * h_2 + sdbar * R) / opgh2;


      // vnh 2/2
      gnh0 = (2 * eksum1 + qbar * vbar * vbar - Nf * kbt) / qnh0;
      vnh0 = vnh0 + gnh0 * h_2;

      eksum0 = eksum1;
      velsc0 = velsc1;
   }


   darray::scale(g::q0, n, velsc0, vx);
   darray::scale(g::q0, n, velsc0, vy);
   darray::scale(g::q0, n, velsc0, vz);
   const double velsc2 = velsc0 * velsc0;
   eksum *= velsc2;
   for (int ii = 0; ii < 3; ++ii)
      for (int jj = 0; jj < 3; ++jj)
         ekin[ii][jj] *= velsc2;
}


void vv_lpiston_npt_v5(int istep, time_prec dt)
{
   int vers1 = rc_flag & calc::vmask;
   bool save = !(istep % inform::iwrite);
   if (!save)
      vers1 &= ~calc::energy;


   const time_prec dt_2 = 0.5 * dt;
   const double R = normal<double>();


   lp_v5(dt, R);
   propagate_velocity(dt_2, gx, gy, gz);


   // volume
   const double term = vbar * dt_2;
   const double expterm = std::exp(term);
   const double eterm2 = expterm * expterm;
   lvec1 *= eterm2;
   lvec2 *= eterm2;
   lvec3 *= eterm2;
   set_default_recip_box();


   constexpr double e2 = 1.0 / 6;
   constexpr double e4 = e2 / 20;
   constexpr double e6 = e4 / 42;
   constexpr double e8 = e6 / 72;
   // sinh(x)/x: Taylor series
   const double term2 = term * term;
   double poly = 1 + term2 * (e2 + term2 * (e4 + term2 * (e6 + term2 * e8)));
   poly *= expterm * dt;
   propagate_pos_axbv(eterm2, poly);
   copy_pos_to_xyz(true);


   energy(vers1);


   propagate_velocity(dt_2, gx, gy, gz);
   lp_v5(dt, R);
}
}


void vv_lpiston_npt(int istep, time_prec dt)
{
   vv_lpiston_npt_v5(istep, dt);
}
}