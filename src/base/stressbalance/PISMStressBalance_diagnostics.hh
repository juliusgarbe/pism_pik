// Copyright (C) 2010, 2011, 2012, 2013, 2014, 2015 Constantine Khroulev
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

#ifndef _PISMSTRESSBALANCE_DIAGNOSTICS_H_
#define _PISMSTRESSBALANCE_DIAGNOSTICS_H_

#include "PISMStressBalance.hh"
#include "base/util/PISMDiagnostic.hh"

namespace pism {
namespace stressbalance {

//! \brief Computes the vertically-averaged ice velocity.
class PSB_velbar : public Diag<StressBalance>
{
public:
  PSB_velbar(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes velbar_mag, the magnitude of vertically-integrated horizontal
//! velocity of ice and masks out ice-free areas.
class PSB_velbar_mag : public Diag<StressBalance>
{
public:
  PSB_velbar_mag(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes uflux and vflux, components of vertically-integrated horizontal
//! flux of ice.
class PSB_flux : public Diag<StressBalance>
{
public:
  PSB_flux(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes flux_mag, the magnitude of vertically-integrated horizontal
//! flux of ice.
class PSB_flux_mag : public Diag<StressBalance>
{
public:
  PSB_flux_mag(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes velbase_mag, the magnitude of horizontal velocity of ice at base
//! of ice and masks out ice-free areas.
class PSB_velbase_mag : public Diag<StressBalance>
{
public:
  PSB_velbase_mag(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes velsurf_mag, the magnitude of horizontal ice velocity at the
//! surface.
class PSB_velsurf_mag : public Diag<StressBalance>
{
public:
  PSB_velsurf_mag(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes velsurf, the horizontal velocity of ice at ice surface.
class PSB_velsurf : public Diag<StressBalance>
{
public:
  PSB_velsurf(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! Computes vertical ice velocity (relative to the geoid).
/*!
  \f[
  w(s) = \tilde w(s) + \frac{\partial b}{\partial t} + U(s) \cdot \nabla b
  \f]
  in grounded areas. In floating shelves
  \f[
  w(s) = \tilde w(s) - \tilde  w(z_{\text{sea level}}).
  \f]

  This ensures that \f$\tilde w(z_{\text{sea level}}) = 0\f$.
*/
class PSB_wvel : public Diag<StressBalance>
{
public:
  PSB_wvel(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! Computes wvelsurf, the vertical velocity of ice at ice surface.
class PSB_wvelsurf : public Diag<StressBalance>
{
public:
  PSB_wvelsurf(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! Computes wvelbase, the vertical velocity of ice at the base of ice.
class PSB_wvelbase : public Diag<StressBalance>
{
public:
  PSB_wvelbase(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes horizontal ice velocity at the base of ice.
class PSB_velbase : public Diag<StressBalance>
{
public:
  PSB_velbase(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes basal frictional heating.
class PSB_bfrict : public Diag<StressBalance>
{
public:
  PSB_bfrict(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes the x-component of the horizontal ice velocity.
class PSB_uvel : public Diag<StressBalance>
{
public:
  PSB_uvel(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes the y-component of the horizontal ice velocity.
class PSB_vvel : public Diag<StressBalance>
{
public:
  PSB_vvel(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Computes vertical velocity of ice, relative to the bed directly
//! below.
class PSB_wvel_rel : public Diag<StressBalance>
{
public:
  PSB_wvel_rel(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the volumetric strain heating (3D).
class PSB_strainheat : public Diag<StressBalance>
{
public:
  PSB_strainheat(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the vertically-integrated (2D) principal strain rates.
class PSB_strain_rates : public Diag<StressBalance>
{
public:
  PSB_strain_rates(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the vertically-integrated (2D) deviatoric stresses.
class PSB_deviatoric_stresses : public Diag<StressBalance>
{
public:
  PSB_deviatoric_stresses(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the pressure within the ice (3D).
class PSB_pressure : public Diag<StressBalance>
{
public:
  PSB_pressure(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the xz component of the shear stress within the ice (3D), according to the SIA formula.
class PSB_tauxz : public Diag<StressBalance>
{
public:
  PSB_tauxz(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};

//! \brief Reports the yz component of the shear stress within the ice (3D), according to the SIA formula.
class PSB_tauyz : public Diag<StressBalance>
{
public:
  PSB_tauyz(StressBalance *m);
  virtual IceModelVec::Ptr compute();
};


} // end of namespace stressbalance
} // end of namespace pism

#endif /* _PISMSTRESSBALANCE_DIAGNOSTICS_H_ */
