// Copyright (C) 2009-2015 Andreas Aschwanden, Ed Bueler and Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include "base/util/pism_const.hh"
#include "enthalpyConverter.hh"
#include "base/util/PISMConfigInterface.hh"
#include "base/varcEnthalpyConverter.hh"

#include "base/util/error_handling.hh"

namespace pism {

/*! @class EnthalpyConverter

  Maps from @f$ (H,p) @f$ to @f$ (T,\omega,p) @f$ and back.

  Requirements:

  1. A converter has to implement an invertible map @f$ (H,p) \to (T,
     \omega, p) @f$ *and* its inverse. Both the forward map and the
     inverse need to be defined for all permissible values of @f$
     (H,p) @f$ and @f$ (T, \omega, p) @f$, respectively.

  2. A converter has to be consistent with laws and parameterizations
     used elsewhere in the model. This includes models coupled to
     PISM.

  3. For a fixed volume of liquid (or solid) water and given two
     energy states @f$ (H_1, p_1) @f$ and @f$ (H_2, p_2) @f$ , let @f$
     \Delta U_H @f$ be the difference in internal energy of this
     volume between the two states *computed using enthalpy*. We require
     that @f$ \Delta U_T = \Delta U_H @f$ , where @f$ \Delta U_T @f$
     is the difference in internal energy *computed using corresponding*
     @f$ (T_1, \omega_1, p_1) @f$ *and* @f$ (T_2, \omega_2, p_2) @f$.

  4. We assume that ice and water are incompressible, so a change in
     pressure does no work, and @f$ \Diff{H}{p} = 0 @f$. In addition
     to this, for cold ice and liquid water @f$ \Diff{T}{p} = 0 @f$.
*/

EnthalpyConverter::EnthalpyConverter(const Config &config) {
  m_beta        = config.get_double("beta_CC"); // K Pa-1
  m_c_i         = config.get_double("ice_specific_heat_capacity"); // J kg-1 K-1
  m_g           = config.get_double("standard_gravity"); // m s-2
  m_L           = config.get_double("water_latent_heat_fusion"); // J kg-1
  m_p_air       = config.get_double("surface_pressure"); // Pa
  m_rho_i       = config.get_double("ice_density"); // kg m-3
  m_T_melting   = config.get_double("water_melting_point_temperature"); // K  
  m_T_tolerance = config.get_double("cold_mode_is_temperate_ice_tolerance"); // K
  m_T_0         = config.get_double("enthalpy_converter_reference_temperature"); // K

  m_do_cold_ice_methods  = config.get_boolean("do_cold_ice_methods");
}

EnthalpyConverter::~EnthalpyConverter() {
  // empty
}

//! Return `true` if ice at `(E, P)` is temperate.
bool EnthalpyConverter::is_temperate(double E, double P) const {
  return this->is_temperate_impl(E, P);
}

//! Return temperature of ice at `(E, P)`.
double EnthalpyConverter::temperature(double E, double P) const {
  return this->temperature_impl(E, P);
}


//! Get pressure in ice from depth below surface using the hydrostatic assumption.
/*! If \f$d\f$ is the depth then
     \f[ p = p_{\text{air}}  + \rho_i g d. \f]
Frequently \f$d\f$ is computed from the thickess minus a level in the ice,
something like `ice_thickness(i, j) - z[k]`.  The input depth to this routine is allowed to
be negative, representing a position above the surface of the ice.
 */
double EnthalpyConverter::pressure(double depth) const {
  if (depth > 0.0) {
    return m_p_air + m_rho_i * m_g * depth;
  } else {
    return m_p_air; // at or above surface of ice
  }
}

//! Specific heat capacity of ice as a function of temperature `T`.
double EnthalpyConverter::c(double T) const {
  return this->c_impl(T);
}

double EnthalpyConverter::c_impl(double /*T*/) const {
  return m_c_i;
}

//! Latent heat of fusion of water as a function of pressure melting
//! temperature.
double EnthalpyConverter::L(double T_m) const {
  return this->L_impl(T_m);
}

double EnthalpyConverter::L_impl(double T_m) const {
  (void) T_m;
  return m_L;
}

//! Get melting temperature from pressure p.
/*!
     \f[ T_m(p) = T_{melting} - \beta p. \f]
 */
double EnthalpyConverter::melting_temperature(double P) const {
  return this->melting_temperature_impl(P);
}

double EnthalpyConverter::melting_temperature_impl(double P) const {
  return m_T_melting - m_beta * P;
}


//! Get enthalpy E_s(p) at cold-temperate transition point from pressure p.
/*! Returns
     \f[ E_s(p) = c_i (T_m(p) - T_0), \f]
 */
double EnthalpyConverter::enthalpy_cts(double P) const {
  return this->enthalpy_cts_impl(P);
}

double EnthalpyConverter::enthalpy_cts_impl(double P) const {
  return m_c_i * (melting_temperature(P) - m_T_0);
}

//! @brief Compute the maximum allowed value of ice enthalpy
//! (corresponds to @f$ \omega = 1 @f$).
double EnthalpyConverter::enthalpy_liquid(double P) const {
  return this->enthalpy_liquid_impl(P);
}

double EnthalpyConverter::enthalpy_liquid_impl(double P) const {
  return enthalpy_cts(P) + L(melting_temperature(P));
}

//! Determines if E >= E_s(p), that is, if the ice is at the pressure-melting point.
bool EnthalpyConverter::is_temperate_impl(double E, double P) const {
  if (m_do_cold_ice_methods) {
    return (pressure_adjusted_temperature(E, P) >= m_T_melting - m_T_tolerance);
  } else {
    return (E >= enthalpy_cts(P));
  }
}


//! Get absolute (not pressure-adjusted) ice temperature (K) from enthalpy and pressure.
/*! From \ref AschwandenBuelerKhroulevBlatter,
     \f[ T= T(E,p) = \begin{cases} 
                       c_i^{-1} E + T_0,  &  E < E_s(p), \\
                       T_m(p),            &  E_s(p) \le E < E_l(p).
                     \end{cases} \f]

We do not allow liquid water (%i.e. water fraction \f$\omega=1.0\f$) so we
throw an exception if \f$E \ge E_l(p)\f$.
 */
double EnthalpyConverter::temperature_impl(double E, double P) const {

#if (PISM_DEBUG==1)
  if (E >= enthalpy_liquid(P)) {
    throw RuntimeError::formatted("E=%f at P=%f equals or exceeds that of liquid water",
                                  E, P);
  }
#endif

  if (E < enthalpy_cts(P)) {
    return (E / m_c_i) + m_T_0;
  } else {
    return melting_temperature(P);
  }
}


//! Get pressure-adjusted ice temperature, in Kelvin, from enthalpy and pressure.
/*!
The pressure-adjusted temperature is:
     \f[ T_{pa}(E,p) = T(E,p) - T_m(p) + T_{melting}. \f]
 */
double EnthalpyConverter::pressure_adjusted_temperature(double E, double P) const {
  return temperature(E, P) - melting_temperature(P) + m_T_melting;
}


//! Get liquid water fraction from enthalpy and pressure.
/*!
  From [@ref AschwandenBuelerKhroulevBlatter],
   @f[
   \omega(E,p) =
   \begin{cases}
     0.0, & E \le E_s(p), \\
     (E-E_s(p)) / L, & E_s(p) < E < E_l(p).
   \end{cases}
   @f]

   We do not allow liquid water (i.e. water fraction @f$ \omega=1.0 @f$).
 */
double EnthalpyConverter::water_fraction(double E, double P) const {
  return this->water_fraction_impl(E, P);
}

double EnthalpyConverter::water_fraction_impl(double E, double P) const {

#if (PISM_DEBUG==1)
  if (E >= enthalpy_liquid(P)) {
    throw RuntimeError::formatted("E=%f and pressure=%f correspond to liquid water",
                                  E, P);
  }
#endif

  double E_s = enthalpy_cts(P);
  if (E <= E_s) {
    return 0.0;
  } else {
    return (E - E_s) / L(melting_temperature(P));
  }
}


//! Compute enthalpy from absolute temperature, liquid water fraction, and pressure.
/*! This is an inverse function to the functions \f$T(E,p)\f$ and
\f$\omega(E,p)\f$ [\ref AschwandenBuelerKhroulevBlatter].  It returns:
  \f[E(T,\omega,p) =
       \begin{cases}
         c_i (T - T_0),     & T < T_m(p) \quad\text{and}\quad \omega = 0, \\
         E_s(p) + \omega L, & T = T_m(p) \quad\text{and}\quad \omega \ge 0.
       \end{cases} \f]
Certain cases are not allowed and throw exceptions:
- \f$T<=0\f$ (error code 1)
- \f$\omega < 0\f$ or \f$\omega > 1\f$ (error code 2)
- \f$T>T_m(p)\f$ (error code 3)
- \f$T<T_m(p)\f$ and \f$\omega > 0\f$ (error code 4)
These inequalities may be violated in the sixth digit or so, however.
 */
double EnthalpyConverter::enthalpy(double T, double omega, double P) const {
  return this->enthalpy_impl(T, omega, P);
}

double EnthalpyConverter::enthalpy_impl(double T, double omega, double P) const {
  const double T_melting = melting_temperature(P);

#if (PISM_DEBUG==1)
  if (T <= 0.0) {
    throw RuntimeError::formatted("T = %f <= 0 is not a valid absolute temperature",T);
  }
  if ((omega < 0.0 - 1.0e-6) || (1.0 + 1.0e-6 < omega)) {
    throw RuntimeError::formatted("water fraction omega=%f not in range [0,1]",omega);
  }
  if (T > T_melting + 1.0e-6) {
    throw RuntimeError::formatted("T=%f exceeds T_melting=%f; not allowed",T,T_melting);
  }
  if ((T < T_melting - 1.0e-6) && (omega > 0.0 + 1.0e-6)) {
    throw RuntimeError::formatted("T < T_melting AND omega > 0 is contradictory;"
                                  " got T=%f, T_melting=%f, omega=%f",
                                  T, T_melting, omega);
  }
#endif

  if (T < T_melting) {
    return m_c_i * (T - m_T_0);
  } else {
    return enthalpy_cts(P) + omega * L(T_melting);
  }
}


//! Compute enthalpy more permissively than enthalpy().
/*! Computes enthalpy from absolute temperature, liquid water fraction, and
pressure.  Use this form of enthalpy() when outside sources (e.g. information
from a coupler) might generate a temperature above the pressure melting point or
generate cold ice with a positive water fraction.

Treats temperatures above pressure-melting point as \e at the pressure-melting
point.  Interprets contradictory case of \f$T < T_m(p)\f$ and \f$\omega > 0\f$
as cold ice, ignoring the water fraction (\f$\omega\f$) value.

Calls enthalpy(), which validates its inputs. 

Computes:
  @f[
  E =
  \begin{cases}
    E(T,0.0,p),         & T < T_m(p) \quad \text{and} \quad \omega \ge 0, \\
    E(T_m(p),\omega,p), & T \ge T_m(p) \quad \text{and} \quad \omega \ge 0,
  \end{cases}
  @f]
  but ensures @f$ 0 \le \omega \le 1 @f$ in second case.  Calls enthalpy() for
  @f$ E(T,\omega,p) @f$.
 */
double EnthalpyConverter::enthalpy_permissive(double T, double omega, double P) const {
  return this->enthalpy_permissive_impl(T, omega, P);
}

double EnthalpyConverter::enthalpy_permissive_impl(double T, double omega, double P) const {

  const double T_m = melting_temperature(P);

  if (T < T_m) {
    return enthalpy(T, 0.0, P);
  } else { // T >= T_m(P) replaced with T = T_m(P)
    return enthalpy(T_m, std::max(0.0, std::min(omega, 1.0)), P);
  }
}

ColdEnthalpyConverter::ColdEnthalpyConverter(const Config &config)
  : EnthalpyConverter(config) {
  m_do_cold_ice_methods = true;
}

ColdEnthalpyConverter::~ColdEnthalpyConverter() {
  // empty
}

double ColdEnthalpyConverter::enthalpy_permissive_impl(double T,
                                                       double /*omega*/,
                                                       double /*pressure*/) const {
  return m_c_i * (T - m_T_0);
}

double ColdEnthalpyConverter::enthalpy_impl(double T, double /*omega*/,
                                            double /*pressure*/) const {
  return m_c_i * (T - m_T_0);
}

double ColdEnthalpyConverter::water_fraction_impl(double /*E*/,
                                                  double /*pressure*/) const {
  return 0.0;
}

double ColdEnthalpyConverter::melting_temperature_impl(double /*pressure*/) const {
  return m_T_melting;
}

bool ColdEnthalpyConverter::is_temperate_impl(double /*E*/, double /*pressure*/) const {
  return false;
}

double ColdEnthalpyConverter::temperature_impl(double E, double /*pressure*/) const {
  return (E / m_c_i) + m_T_0;
}

/*! @class KirchhoffEnthalpyConverter

  Following a re-interpretation of [@ref
  AschwandenBuelerKhroulevBlatter], we require that @f$ \Diff{H}{p} =
  0 @f$:

  @f[
  \Diff{H}{p} = \diff{H_w}{p} + \diff{H_w}{p}\Diff{T}{p}
  @f]

  We assume that water is incompressible, so @f$ \Diff{T}{p} = 0 @f$
  and the second term vanishes.

  As for the first term, equation (5) of [@ref
  AschwandenBuelerKhroulevBlatter] defines @f$ H_w @f$ as follows:

  @f[
  H_w = \int_{T_0}^{T_m(p)} C_i(t) dt + L + \int_{T_m(p)}^T C_w(t)dt  
  @f]

  Using the fundamental theorem of Calculus, we get
  @f[
  \diff{H_w}{p} = (C_i(T_m(p)) - C_w(T_m(p))) \diff{T_m(p)}{p} + \diff{L}{p}
  @f]

  Assuming that @f$ C_i(T) = c_i @f$ and @f$ C_w(T) = c_w @f$ (i.e. specific heat
  capacities of ice and water do not depend on temperature) and using
  the Clausius-Clapeyron relation
  @f[
  T_m(p) = T_m(p_{\text{air}}) - \beta p,  
  @f]

  we get
  @f{align}{
  \Diff{H}{p} &= (c_i - c_w)\diff{T_m(p)}{p} + \diff{L}{p}\\
  &= \beta(c_w - c_i) + \diff{L}{p}\\
  @f}
  Requiring @f$ \Diff{H}{p} = 0 @f$ implies
  @f[
  \diff{L}{p} = -\beta(c_w - c_i),
  @f]
  and so
  @f{align}{
  L(p) &= -\beta p (c_w - c_i) + C\\
  &= (T_m(p) - T_m(p_{\text{air}})) (c_w - c_i) + C.
  @f}

  Letting @f$ p = p_{\text{air}} @f$ we find @f$ C = L(p_\text{air}) = L_0 @f$, so
  @f[
  L(p) = (T_m(p) - T_m(p_{\text{air}})) (c_w - c_i) + L_0,
  @f]
  where @f$ L_0 @f$ is the latent heat of fusion of water at atmospheric
  pressure.

  Therefore a consistent interpretation of [@ref
  AschwandenBuelerKhroulevBlatter] requires the temperature-dependent
  approximation of the latent heat of fusion of water given above.

  Note that this form of @f$ L(p) @f$ also follows from Kirchhoff's
  law of thermochemistry.
*/

//! Converter using Kirchhoff's law of thermochemistry.
KirchhoffEnthalpyConverter::KirchhoffEnthalpyConverter(const Config &config)
  : EnthalpyConverter(config) {
  m_c_w = config.get_double("water_specific_heat_capacity"); // J kg-1 K-1
}

KirchhoffEnthalpyConverter::~KirchhoffEnthalpyConverter() {
  // empty
}

//! @brief Latent heat of fusion of water using Kirchhoff's law of
//! thermochemistry.
double KirchhoffEnthalpyConverter::L_impl(double T_pm) const {
  return m_L + (m_c_w - m_c_i) * (T_pm - 273.15);
}

EnthalpyConverter::Ptr enthalpy_converter_from_options(const Config &config) {
  EnthalpyConverter *EC = NULL;

  if (config.get_boolean("use_linear_in_temperature_heat_capacity")) {
    EC = new varcEnthalpyConverter(config);
  } else if (config.get_boolean("use_Kirchhoff_law")) {
    EC = new KirchhoffEnthalpyConverter(config);
  } else {
    EC = new EnthalpyConverter(config);
  }

  return EnthalpyConverter::Ptr(EC);
}

} // end of namespace pism
