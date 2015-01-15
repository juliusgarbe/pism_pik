// Copyright (C) 2010, 2011, 2012, 2013, 2014 Constantine Khroulev and Ed Bueler
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

#ifndef _PISMSTRESSBALANCE_H_
#define _PISMSTRESSBALANCE_H_

#include "PISMComponent.hh"     // derives from PISMComponent
#include "iceModelVec.hh"

class ShallowStressBalance;
class SSB_Modifier;
class PISMDiagnostic;
class PISMOceanModel;

//! The class defining PISM's interface to the shallow stress balance code.
/*!
Generally all the nontrivial fields are updated by a call to update().  The rest
of the methods generally provide access to precomputed results.  The following
diagram shows where these results are generally used in the rest of PISM.  (It 
does not show the call graph, as would doxygen.)

\image html stressbalance-out.png "\b Methods of PISMStressBalance, and the uses of their results.  Dotted edges show scalars and dashed edges show fields.  Dashed boxes inside the PISMStressBalance object are important methods which may be present in shallow cases.  The age time step has inputs which are a strict subset of the inputs of the energy time step."

this command fails: \dotfile stressbalance-out.dot
 */
class PISMStressBalance : public PISMComponent
{
public:
  PISMStressBalance(IceGrid &g, ShallowStressBalance *sb, SSB_Modifier *ssb_mod,
                    const PISMConfig &config);
  virtual ~PISMStressBalance();

  //! \brief Initialize the PISMStressBalance object.
  virtual PetscErrorCode init(PISMVars &vars);

  //! \brief Adds more variable names to result (to respect -o_size and
  //! -save_size).
  /*!
    Keyword can be one of "small", "medium" or "big".
   */
  virtual void add_vars_to_output(std::string keyword, std::set<std::string> &result);

  //! Defines requested fields to file and/or asks an attached
  //! model to do so.
  virtual PetscErrorCode define_variables(std::set<std::string> /*vars*/, const PIO &/*nc*/,
                                          PISM_IO_Type /*nctype*/);

  //! Writes requested fields to a file.
  virtual PetscErrorCode write_variables(std::set<std::string> vars, const PIO &nc);

  //! \brief Set the vertically-averaged ice velocity boundary condition.
  /*!
   * Does not affect the SIA computation.
   */
  virtual PetscErrorCode set_boundary_conditions(IceModelVec2Int &locations,
                                                 IceModelVec2V &velocities);

  virtual PetscErrorCode set_basal_melt_rate(IceModelVec2S *bmr);

  //! \brief Update all the fields if fast == false, only update diffusive flux
  //! and max. diffusivity otherwise.
  virtual PetscErrorCode update(bool fast, double sea_level,
                                IceModelVec2S &melange_back_pressure);

  //! \brief Get the thickness-advective (SSA) 2D velocity.
  virtual PetscErrorCode get_2D_advective_velocity(IceModelVec2V* &result);

  //! \brief Get the diffusive (SIA) vertically-averaged flux on the staggered grid.
  virtual PetscErrorCode get_diffusive_flux(IceModelVec2Stag* &result);

  //! \brief Get the max diffusivity (for the adaptive time-stepping).
  virtual PetscErrorCode get_max_diffusivity(double &D);

  // for the energy/age time step:

  //! \brief Get the 3D velocity (for the energy/age time-stepping).
  virtual PetscErrorCode get_3d_velocity(IceModelVec3* &u, IceModelVec3* &v, IceModelVec3* &w);

  //! \brief Get the basal frictional heating (for the energy time-stepping).
  virtual PetscErrorCode get_basal_frictional_heating(IceModelVec2S* &result);

  virtual PetscErrorCode get_volumetric_strain_heating(IceModelVec3* &result);

  // for the calving, etc.:

  //! \brief Get the largest and smallest eigenvalues of the strain rate tensor.
  virtual PetscErrorCode compute_2D_principal_strain_rates(IceModelVec2V &velocity, IceModelVec2Int &mask,
                                                           IceModelVec2 &result);

  //! \brief Get the components of the 2D deviatoric stress tensor.
  virtual PetscErrorCode compute_2D_stresses(IceModelVec2V &velocity, IceModelVec2Int &mask,
                                             IceModelVec2 &result);

  //! \brief Produce a report string for the standard output.
  virtual PetscErrorCode stdout_report(std::string &result);

  //! \brief Extends the computational grid (vertically).
  virtual PetscErrorCode extend_the_grid(int old_Mz);

  virtual void get_diagnostics(std::map<std::string, PISMDiagnostic*> &dict,
                               std::map<std::string, PISMTSDiagnostic*> &ts_dict);

  //! \brief Returns a pointer to a stress balance solver implementation.
  virtual ShallowStressBalance* get_stressbalance()
  { return m_stress_balance; }

  //! \brief Returns a pointer to a stress balance modifier implementation.
  virtual SSB_Modifier* get_ssb_modifier()
  { return m_modifier; }
protected:
  virtual PetscErrorCode allocate();
  virtual PetscErrorCode compute_vertical_velocity(IceModelVec3 *u, IceModelVec3 *v,
                                                   IceModelVec2S *bmr, IceModelVec3 &result);
  virtual PetscErrorCode compute_volumetric_strain_heating();

  PISMVars *m_variables;

  IceModelVec3 m_w, m_strain_heating;
  IceModelVec2S *m_basal_melt_rate;

  ShallowStressBalance *m_stress_balance;
  SSB_Modifier *m_modifier;
};

#endif /* _PISMSTRESSBALANCE_H_ */

