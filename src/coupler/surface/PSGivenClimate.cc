// Copyright (C) 2011, 2012, 2013, 2014, 2015 PISM Authors
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

#include <gsl/gsl_math.h>

#include "PSGivenClimate.hh"
#include "base/util/IceGrid.hh"

namespace pism {
namespace surface {

Given::Given(IceGrid::ConstPtr g)
  : PGivenClimate<SurfaceModifier,SurfaceModel>(g, NULL)
{
  option_prefix = "-surface_given";

  ice_surface_temp      = new IceModelVec2T;
  climatic_mass_balance = new IceModelVec2T;

  m_fields["ice_surface_temp"]      = ice_surface_temp;
  m_fields["climatic_mass_balance"] = climatic_mass_balance;

  process_options();

  std::map<std::string, std::string> standard_names;
  standard_names["climatic_mass_balance"] = "land_ice_surface_specific_mass_balance_flux";
  set_vec_parameters(standard_names);

  ice_surface_temp->create(m_grid, "ice_surface_temp");
  climatic_mass_balance->create(m_grid, "climatic_mass_balance");

  ice_surface_temp->set_attrs("climate_forcing",
                              "temperature of the ice at the ice surface but below firn processes",
                              "Kelvin", "");
  climatic_mass_balance->set_attrs("climate_forcing",
                                   "surface mass balance (accumulation/ablation) rate",
                                   "kg m-2 s-1", "land_ice_surface_specific_mass_balance_flux");
  climatic_mass_balance->metadata().set_string("glaciological_units", "kg m-2 year-1");
  climatic_mass_balance->write_in_glaciological_units = true;
}

Given::~Given() {
  // empty
}

void Given::attach_atmosphere_model_impl(atmosphere::AtmosphereModel *input) {
  delete input;
  input = NULL;
}

void Given::init_impl() {

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  m_log->message(2,
             "* Initializing the surface model reading temperature at the top of the ice\n"
             "  and ice surface mass flux from a file...\n");

  ice_surface_temp->init(filename, bc_period, bc_reference_time);
  climatic_mass_balance->init(filename, bc_period, bc_reference_time);

  // read time-independent data right away:
  if (ice_surface_temp->get_n_records() == 1 && climatic_mass_balance->get_n_records() == 1) {
    update(m_grid->ctx()->time()->current(), 0); // dt is irrelevant
  }
}

void Given::update_impl(double my_t, double my_dt) {
  update_internal(my_t, my_dt);

  climatic_mass_balance->average(m_t, m_dt);
  ice_surface_temp->average(m_t, m_dt);
}

void Given::ice_surface_mass_flux_impl(IceModelVec2S &result) {
  result.copy_from(*climatic_mass_balance);
}

void Given::ice_surface_temperature_impl(IceModelVec2S &result) {
  result.copy_from(*ice_surface_temp);
}

} // end of namespace surface
} // end of namespace pism
