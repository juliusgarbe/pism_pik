// Copyright (C) 2011, 2012, 2013, 2014 PISM Authors
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

#include "PSLapseRates.hh"

PSLapseRates::PSLapseRates(IceGrid &g, const PISMConfig &conf, PISMSurfaceModel* in)
  : PLapseRates<PISMSurfaceModel,PSModifier>(g, conf, in),
    climatic_mass_balance(g.get_unit_system()),
    ice_surface_temp(g.get_unit_system())
{
  smb_lapse_rate = 0;
  option_prefix = "-surface_lapse_rate";

  PetscErrorCode ierr = allocate_PSLapseRates(); CHKERRCONTINUE(ierr);
  if (ierr != 0)
    PISMEnd();

}

PSLapseRates::~PSLapseRates() {
  // empty
}

PetscErrorCode PSLapseRates::allocate_PSLapseRates() {
  PetscErrorCode ierr;

  climatic_mass_balance.init_2d("climatic_mass_balance", grid);
  climatic_mass_balance.set_string("pism_intent", "diagnostic");
  climatic_mass_balance.set_string("long_name",
                  "surface mass balance (accumulation/ablation) rate");
  climatic_mass_balance.set_string("standard_name",
                  "land_ice_surface_specific_mass_balance");
  ierr = climatic_mass_balance.set_units("kg m-2 s-1"); CHKERRQ(ierr);
  ierr = climatic_mass_balance.set_glaciological_units("kg m-2 year-1"); CHKERRQ(ierr);

  ice_surface_temp.init_2d("ice_surface_temp", grid);
  ice_surface_temp.set_string("pism_intent", "diagnostic");
  ice_surface_temp.set_string("long_name",
                              "ice temperature at the ice surface");
  ierr = ice_surface_temp.set_units("K"); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode PSLapseRates::init(PISMVars &vars) {
  PetscErrorCode ierr;
  bool smb_lapse_rate_set;

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  ierr = input_model->init(vars); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "  [using temperature and mass balance lapse corrections]\n"); CHKERRQ(ierr);

  ierr = init_internal(vars); CHKERRQ(ierr);

  ierr = PetscOptionsBegin(grid.com, "", "Lapse rate options", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsReal("-smb_lapse_rate",
                           "Elevation lapse rate for the surface mass balance, in m/year per km",
                           smb_lapse_rate, smb_lapse_rate_set); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com,
                    "   ice upper-surface temperature lapse rate: %3.3f K per km\n"
                    "   ice-equivalent surface mass balance lapse rate: %3.3f m/year per km\n",
                    temp_lapse_rate, smb_lapse_rate); CHKERRQ(ierr);

  temp_lapse_rate = grid.convert(temp_lapse_rate, "K/km", "K/m");

  smb_lapse_rate *= config.get("ice_density"); // convert from [m/year / km] to [kg m-2 / year / km]
  smb_lapse_rate = grid.convert(smb_lapse_rate, "(kg m-2) / year / km", "(kg m-2) / s / m");

  return 0;
}

PetscErrorCode PSLapseRates::ice_surface_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = input_model->ice_surface_mass_flux(result); CHKERRQ(ierr);
  ierr = lapse_rate_correction(result, smb_lapse_rate); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode PSLapseRates::ice_surface_temperature(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = input_model->ice_surface_temperature(result); CHKERRQ(ierr);
  ierr = lapse_rate_correction(result, temp_lapse_rate); CHKERRQ(ierr);
  return 0;
}

void PSLapseRates::add_vars_to_output(std::string keyword, std::set<std::string> &result) {
  if (keyword == "medium" || keyword == "big") {
    result.insert("ice_surface_temp");
    result.insert("climatic_mass_balance");
  }

  input_model->add_vars_to_output(keyword, result);
}

PetscErrorCode PSLapseRates::define_variables(std::set<std::string> vars, const PIO &nc, PISM_IO_Type nctype) {
  PetscErrorCode ierr;

  if (set_contains(vars, "ice_surface_temp")) {
    ierr = ice_surface_temp.define(nc, nctype, true); CHKERRQ(ierr);
  }

  if (set_contains(vars, "climatic_mass_balance")) {
    ierr = climatic_mass_balance.define(nc, nctype, true); CHKERRQ(ierr);
  }

  ierr = input_model->define_variables(vars, nc, nctype); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode PSLapseRates::write_variables(std::set<std::string> vars, const PIO &nc) {
  PetscErrorCode ierr;

  if (set_contains(vars, "ice_surface_temp")) {
    IceModelVec2S tmp;
    ierr = tmp.create(grid, "ice_surface_temp", WITHOUT_GHOSTS); CHKERRQ(ierr);
    tmp.metadata() = ice_surface_temp;

    ierr = ice_surface_temperature(tmp); CHKERRQ(ierr);

    ierr = tmp.write(nc); CHKERRQ(ierr);

    vars.erase("ice_surface_temp");
  }

  if (set_contains(vars, "climatic_mass_balance")) {
    IceModelVec2S tmp;
    ierr = tmp.create(grid, "climatic_mass_balance", WITHOUT_GHOSTS); CHKERRQ(ierr);
    tmp.metadata() = climatic_mass_balance;

    ierr = ice_surface_mass_flux(tmp); CHKERRQ(ierr);
    tmp.write_in_glaciological_units = true;
    ierr = tmp.write(nc); CHKERRQ(ierr);

    vars.erase("climatic_mass_balance");
  }

  ierr = input_model->write_variables(vars, nc); CHKERRQ(ierr);

  return 0;
}

