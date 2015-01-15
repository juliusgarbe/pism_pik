// Copyright (C) 2004-2014 Jed Brown, Ed Bueler and Constantine Khroulev
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

#include "IceGrid.hh"
#include "iceModel.hh"
#include "iceEISModel.hh"
#include "SIAFD.hh"
#include "SIA_Sliding.hh"
#include "PISMStressBalance.hh"
#include "pism_options.hh"
#include "POConstant.hh"
#include "PS_EISMINTII.hh"

IceEISModel::IceEISModel(IceGrid &g, PISMConfig &conf, PISMConfig &conf_overrides)
  : IceModel(g, conf, conf_overrides) {
  m_experiment = 'A';

  // the following flag must be here in constructor because
  // IceModel::createVecs() uses it non-polythermal methods; can be
  // overridden by the command-line option "-energy enthalpy"
  config.set_flag("do_cold_ice_methods", true);

  // see EISMINT II description; choose no ocean interaction, 
  config.set_flag("is_dry_simulation", true);

  // purely SIA, and E=1
  config.set_double("sia_enhancement_factor", 1.0);

  // none use bed smoothing & bed roughness parameterization
  config.set_double("bed_smoother_range", 0.0);

  // basal melt does not change computation of mass continuity or vertical velocity:
  config.set_flag("include_bmr_in_continuity", false);

  // Make bedrock thermal material properties into ice properties.  Note that
  // zero thickness bedrock layer is the default, but we want the ice/rock
  // interface segment to have geothermal flux applied directly to ice without
  // jump in material properties at base.
  config.set_double("bedrock_thermal_density", config.get("ice_density"));
  config.set_double("bedrock_thermal_conductivity", config.get("ice_thermal_conductivity"));
  config.set_double("bedrock_thermal_specific_heat_capacity", config.get("ice_specific_heat_capacity"));
}

PetscErrorCode IceEISModel::set_grid_defaults() {
  grid.Lx = 750e3;
  grid.Ly = 750e3;
  grid.Lz = 4e3;  // depend on auto-expansion to handle bigger thickness

  PetscErrorCode ierr = grid.time->init(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IceEISModel::setFromOptions() {
  PetscErrorCode      ierr;

  // set experiment name using command-line options
  {
    std::string name = "A";
    char temp = m_experiment;
    bool EISIIchosen;
    ierr = PISMOptionsString("-eisII", "EISMINT II experiment name",
                             name, EISIIchosen);
    CHKERRQ(ierr);
    if (EISIIchosen == PETSC_TRUE) {
      temp = (char)toupper(name.c_str()[0]);
      if ((temp >= 'A') && (temp <= 'L')) {
        m_experiment = temp;
      } else {
        ierr = PetscPrintf(grid.com,
                           "option -eisII must have value A, B, C, D, E, F, G, H, I, J, K, or L\n");
        CHKERRQ(ierr);
        PISMEnd();
      }
    }

    char tempstr[2] = {temp, 0};
    config.set_string("EISMINT_II_experiment", tempstr);
  }

  ierr = IceModel::setFromOptions();  CHKERRQ(ierr);

  return 0;
}


//! \brief Decide which stress balance model to use.
PetscErrorCode IceEISModel::allocate_stressbalance() {
  PetscErrorCode ierr;

  if (stress_balance == NULL) {
    ShallowStressBalance *my_stress_balance;

    SSB_Modifier *modifier = new SIAFD(grid, *EC, config);

    if (m_experiment == 'G' || m_experiment == 'H') {
      my_stress_balance = new SIA_Sliding(grid, *EC, config);
    } else {
      my_stress_balance = new ZeroSliding(grid, *EC, config);
    }
  
    // ~PISMStressBalance() will de-allocate my_stress_balance and modifier.
    stress_balance = new PISMStressBalance(grid, my_stress_balance,
                                           modifier, config);

    // Note that in PISM stress balance computations are diagnostic, i.e. do not
    // have a state that changes in time. This means that this call can be here
    // and not in model_state_setup() and we don't need to re-initialize after
    // the "diagnostic time step".
    ierr = stress_balance->init(variables); CHKERRQ(ierr);

    if (config.get_flag("include_bmr_in_continuity")) {
      ierr = stress_balance->set_basal_melt_rate(&basal_melt_rate); CHKERRQ(ierr);
    }
  }
  
  return 0;
}

PetscErrorCode IceEISModel::allocate_couplers() {

  // Climate will always come from intercomparison formulas.
  if (surface == NULL)
    surface = new PS_EISMINTII(grid, config, m_experiment);

  if (ocean == NULL)
    ocean = new POConstant(grid, config);

  return 0;
}

PetscErrorCode IceEISModel::generateTroughTopography() {
  PetscErrorCode  ierr;
  // computation based on code by Tony Payne, 6 March 1997:
  // http://homepages.vub.ac.be/~phuybrec/eismint/topog2.f
  
  const double b0    = 1000.0;  // plateau elevation
  const double L     = 750.0e3; // half-width of computational domain
  const double w     = 200.0e3; // trough width
  const double slope = b0/L;
  const double dx61  = (2*L) / 60; // = 25.0e3

  ierr = bed_topography.begin_access(); CHKERRQ(ierr);
  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const double nsd = i * grid.dx, ewd = j * grid.dy;
      if ((nsd >= (27 - 1) * dx61) && (nsd <= (35 - 1) * dx61) &&
          (ewd >= (31 - 1) * dx61) && (ewd <= (61 - 1) * dx61)) {
        bed_topography(i,j) = 1000.0 - PetscMax(0.0, slope * (ewd - L) * cos(M_PI * (nsd - L) / w));
      } else {
        bed_topography(i,j) = 1000.0;
      }
    }
  }
  ierr = bed_topography.end_access(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IceEISModel::generateMoundTopography() {
  PetscErrorCode  ierr;
  // computation based on code by Tony Payne, 6 March 1997:
  // http://homepages.vub.ac.be/~phuybrec/eismint/topog2.f
  
  const double slope = 250.0;
  const double w     = 150.0e3; // mound width

  ierr = bed_topography.begin_access(); CHKERRQ(ierr);
  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
      const double nsd = i * grid.dx, ewd = j * grid.dy;
      bed_topography(i,j) = PetscAbs(slope * sin(M_PI * ewd / w) + slope * cos(M_PI * nsd / w));
    }
  }
  ierr = bed_topography.end_access(); CHKERRQ(ierr);

  return 0;
}


//! Only executed if NOT initialized from file (-i).
PetscErrorCode IceEISModel::set_vars_from_options() {
  PetscErrorCode ierr;

  // initialize from EISMINT II formulas
  ierr = verbPrintf(2, grid.com,
                    "initializing variables from EISMINT II experiment %c formulas... \n", 
                    m_experiment); CHKERRQ(ierr);

  if ((m_experiment == 'I') || (m_experiment == 'J')) {
    ierr = generateTroughTopography(); CHKERRQ(ierr);
  } 
  if ((m_experiment == 'K') || (m_experiment == 'L')) {
    ierr = generateMoundTopography(); CHKERRQ(ierr);
  } 

  // communicate b in any case; it will be horizontally-differentiated
  ierr = bed_topography.update_ghosts(); CHKERRQ(ierr);

  ierr = basal_melt_rate.set(0.0);   CHKERRQ(ierr); 
  ierr = geothermal_flux.set(0.042); CHKERRQ(ierr); // EISMINT II value; J m-2 s-1
  ierr = bed_uplift_rate.set(0.0);   CHKERRQ(ierr); // no experiments have uplift at start
  ierr = ice_thickness.set(0.0);     CHKERRQ(ierr); // start with zero ice

  // regrid 2D variables
  ierr = regrid(2); CHKERRQ(ierr);
  
  // this IceModel bootstrap method should do right thing because of
  // variable settings above and init of coupler above
  ierr = putTempAtDepth(); CHKERRQ(ierr);

  // regrid 3D variables
  ierr = regrid(3); CHKERRQ(ierr);

  return 0;
}

