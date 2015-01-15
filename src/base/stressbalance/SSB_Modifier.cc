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

#include "SSB_Modifier.hh"
#include "flowlaws.hh"
#include "PISMVars.hh"
#include "IceGrid.hh"
#include "flowlaw_factory.hh"
#include "PISMConfig.hh"

PetscErrorCode SSB_Modifier::allocate() {
  PetscErrorCode ierr;

  ierr =     u.create(grid, "uvel", WITH_GHOSTS); CHKERRQ(ierr);
  ierr =     u.set_attrs("diagnostic", "horizontal velocity of ice in the X direction",
                          "m s-1", "land_ice_x_velocity"); CHKERRQ(ierr);
  ierr =     u.set_glaciological_units("m year-1"); CHKERRQ(ierr);
  u.write_in_glaciological_units = true;

  ierr =     v.create(grid, "vvel", WITH_GHOSTS); CHKERRQ(ierr);
  ierr =     v.set_attrs("diagnostic", "horizontal velocity of ice in the Y direction",
                          "m s-1", "land_ice_y_velocity"); CHKERRQ(ierr);
  ierr =     v.set_glaciological_units("m year-1"); CHKERRQ(ierr);
  v.write_in_glaciological_units = true;

  ierr = strain_heating.create(grid, "strainheat", WITHOUT_GHOSTS); CHKERRQ(ierr); // never diff'ed in hor dirs
  ierr = strain_heating.set_attrs("internal",
                          "rate of strain heating in ice (dissipation heating)",
                          "W m-3", ""); CHKERRQ(ierr);
  ierr = strain_heating.set_glaciological_units("mW m-3"); CHKERRQ(ierr);

  ierr = diffusive_flux.create(grid, "diffusive_flux", WITH_GHOSTS, 1); CHKERRQ(ierr);
  ierr = diffusive_flux.set_attrs("internal", 
                                  "diffusive (SIA) flux components on the staggered grid",
                                  "", ""); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode SSB_Modifier::extend_the_grid(int old_Mz) {
  PetscErrorCode ierr;

  ierr =     u.extend_vertically(old_Mz, 0.0); CHKERRQ(ierr);
  ierr =     v.extend_vertically(old_Mz, 0.0); CHKERRQ(ierr);
  ierr = strain_heating.extend_vertically(old_Mz, 0.0); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode ConstantInColumn::init(PISMVars &vars) {
  PetscErrorCode ierr;

  ierr = SSB_Modifier::init(vars); CHKERRQ(ierr);

  return 0;
}

ConstantInColumn::ConstantInColumn(IceGrid &g, EnthalpyConverter &e, const PISMConfig &c)
  : SSB_Modifier(g, e, c)
{
  IceFlowLawFactory ice_factory(grid.com, "", config, &EC);

  ice_factory.setType(config.get_string("sia_flow_law"));

  ice_factory.setFromOptions();
  ice_factory.create(&flow_law);
}

ConstantInColumn::~ConstantInColumn()
{
  if (flow_law != NULL) {
    delete flow_law;
    flow_law = NULL;
  }
}


//! \brief Distribute the input velocity throughout the column.
/*!
 * Things to update:
 * - 3D-distributed horizontal velocity
 * - maximum horizontal velocity
 * - diffusive ice flux
 * - maximum diffusivity
 * - strain heating (strain_heating)
 */
PetscErrorCode ConstantInColumn::update(IceModelVec2V *vel_input, bool fast) {
  PetscErrorCode ierr;

  if (fast)
    return 0;

  // horizontal velocity and its maximum:
  ierr = u.begin_access(); CHKERRQ(ierr);
  ierr = v.begin_access(); CHKERRQ(ierr);
  ierr = vel_input->begin_access(); CHKERRQ(ierr);
  for (int   i = grid.xs; i < grid.xs+grid.xm; ++i) {
    for (int j = grid.ys; j < grid.ys+grid.ym; ++j) {
      ierr = u.setColumn(i,j, (*vel_input)(i,j).u); CHKERRQ(ierr);
      ierr = v.setColumn(i,j, (*vel_input)(i,j).v); CHKERRQ(ierr);
    }
  }
  ierr = vel_input->end_access(); CHKERRQ(ierr);
  ierr = v.end_access(); CHKERRQ(ierr);
  ierr = u.end_access(); CHKERRQ(ierr);  

  // Communicate to get ghosts (needed to compute w):
  ierr = u.update_ghosts(); CHKERRQ(ierr);
  ierr = v.update_ghosts(); CHKERRQ(ierr);

  // diffusive flux and maximum diffusivity
  ierr = diffusive_flux.set(0.0); CHKERRQ(ierr);
  D_max = 0.0;

  return 0;
}
