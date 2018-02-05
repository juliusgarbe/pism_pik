/* Copyright (C) 2013, 2014, 2015, 2016, 2017, 2018 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <gsl/gsl_math.h>       // GSL_NAN

#include "pism/coupler/OceanModel.hh"
#include "pism/util/iceModelVec.hh"
#include "pism/util/MaxTimestep.hh"
#include "pism/util/pism_utilities.hh"

namespace pism {
namespace ocean {

IceModelVec2S::Ptr OceanModel::allocate_sea_level_elevation(IceGrid::ConstPtr g) {
  IceModelVec2S::Ptr result(new IceModelVec2S(g, "sea_level", WITHOUT_GHOSTS));
  result->set_attrs("diagnostic",
                    "sea level elevation, relative to the geoid",
                    "meter", "");
  return result;
}

IceModelVec2S::Ptr OceanModel::allocate_shelf_base_temperature(IceGrid::ConstPtr g) {
  IceModelVec2S::Ptr result(new IceModelVec2S(g, "shelfbtemp", WITHOUT_GHOSTS));
  result->set_attrs("diagnostic",
                    "ice temperature at the bottom of floating ice",
                    "Kelvin", "");
  return result;
}

IceModelVec2S::Ptr OceanModel::allocate_shelf_base_mass_flux(IceGrid::ConstPtr g) {
  IceModelVec2S::Ptr result(new IceModelVec2S(g, "shelfbmassflux", WITHOUT_GHOSTS));

  result->set_attrs("diagnostic", "shelf base mass flux",
                    "kg m-2 s-1", "");
  result->metadata().set_string("glaciological_units", "kg m-2 year-1");

  return result;
}

IceModelVec2S::Ptr OceanModel::allocate_melange_back_pressure(IceGrid::ConstPtr g) {
  IceModelVec2S::Ptr result(new IceModelVec2S(g,
                                              "melange_back_pressure_fraction", WITHOUT_GHOSTS));

  result->set_attrs("diagnostic",
                    "melange back pressure fraction",
                    "1", "");
  result->set(0.0);

  return result;
}

// "modifier" constructor
OceanModel::OceanModel(IceGrid::ConstPtr g, std::shared_ptr<OceanModel> input)
  : Component(g), m_input_model(input) {

  if (not input) {
    m_melange_back_pressure_fraction = allocate_melange_back_pressure(g);
    // set the default value
    m_melange_back_pressure_fraction->set(0.0);
  }
}

// "model" constructor
OceanModel::OceanModel(IceGrid::ConstPtr g)
  : OceanModel(g, nullptr) {
  // empty
}


OceanModel::~OceanModel() {
  // empty
}

void OceanModel::init() {
  m_t  = m_dt = GSL_NAN;        // every re-init restarts the clock
  this->init_impl();
}

void OceanModel::update(double t, double dt) {
  this->update_impl(t, dt);
  m_t  = t;
  m_dt = dt;
}


const IceModelVec2S& OceanModel::shelf_base_mass_flux() const {
  return shelf_base_mass_flux_impl();
}

const IceModelVec2S& OceanModel::sea_level_elevation() const {
  return sea_level_elevation_impl();
}

const IceModelVec2S& OceanModel::shelf_base_temperature() const {
  return shelf_base_temperature_impl();
}

const IceModelVec2S& OceanModel::melange_back_pressure_fraction() const {
  return melange_back_pressure_fraction_impl();
}

// pass-through default implementations for "modifiers"

void OceanModel::update_impl(double t, double dt) {
  if (m_input_model) {
    m_input_model->update(t, dt);
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no input model");
  }
}

MaxTimestep OceanModel::max_timestep_impl(double t) const {
  if (m_input_model) {
    return m_input_model->max_timestep(t);
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no input model");
  }
}


const IceModelVec2S& OceanModel::sea_level_elevation_impl() const {
  if (m_input_model) {
    return m_input_model->sea_level_elevation();
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no input model");
  }
}

const IceModelVec2S& OceanModel::shelf_base_temperature_impl() const {
  if (m_input_model) {
    return m_input_model->shelf_base_temperature();
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no input model");
  }
}

const IceModelVec2S& OceanModel::shelf_base_mass_flux_impl() const {
  if (m_input_model) {
    return m_input_model->shelf_base_mass_flux();
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION, "no input model");
  }
}

const IceModelVec2S& OceanModel::melange_back_pressure_fraction_impl() const {
  if (m_input_model) {
    return m_input_model->melange_back_pressure_fraction();
  } else {
    return *m_melange_back_pressure_fraction;
  }
}

namespace diagnostics {

/*! @brief Sea level elevation. */
class PO_sea_level : public Diag<OceanModel>
{
public:
  PO_sea_level(const OceanModel *m)
    : Diag<OceanModel>(m) {

    /* set metadata: */
    m_vars = {SpatialVariableMetadata(m_sys, "sea_level")};

    set_attrs("sea level elevation, relative to the geoid", "",
              "meters", "meters", 0);
  }

protected:
  IceModelVec::Ptr compute_impl() const {

    IceModelVec2S::Ptr result(new IceModelVec2S(m_grid, "sea_level", WITHOUT_GHOSTS));
    result->metadata(0) = m_vars[0];

    result->copy_from(model->sea_level_elevation());

    return result;
  }

};

/*! @brief Shelf base temperature. */
class PO_shelf_base_temperature : public Diag<OceanModel>
{
public:
  PO_shelf_base_temperature(const OceanModel *m)
    : Diag<OceanModel>(m) {

    /* set metadata: */
    m_vars = {SpatialVariableMetadata(m_sys, "shelfbtemp")};

    set_attrs("ice temperature at the basal surface of ice shelves", "",
              "Kelvin", "Kelvin", 0);
  }
protected:
  IceModelVec::Ptr compute_impl() const {

    IceModelVec2S::Ptr result(new IceModelVec2S(m_grid, "shelfbtemp", WITHOUT_GHOSTS));
    result->metadata(0) = m_vars[0];

    result->copy_from(model->shelf_base_temperature());

    return result;
  }
};



/*! @brief Shelf base mass flux. */
class PO_shelf_base_mass_flux : public Diag<OceanModel>
{
public:
  PO_shelf_base_mass_flux(const OceanModel *m)
    : Diag<OceanModel>(m) {

    /* set metadata: */
    m_vars = {SpatialVariableMetadata(m_sys, "shelfbmassflux")};

    set_attrs("mass flux at the basal surface of ice shelves", "",
              "kg m-2 s-1", "kg m-2 s-1", 0);
  }
protected:
  IceModelVec::Ptr compute_impl() const {

    IceModelVec2S::Ptr result(new IceModelVec2S(m_grid, "shelfbmassflux", WITHOUT_GHOSTS));
    result->metadata(0) = m_vars[0];

    result->copy_from(model->shelf_base_mass_flux());

    return result;
  }
};



/*! @brief Melange back pressure fraction. */
class PO_melange_back_pressure_fraction : public Diag<OceanModel>
{
public:
  PO_melange_back_pressure_fraction(const OceanModel *m)
    : Diag<OceanModel>(m) {

    /* set metadata: */
    m_vars = {SpatialVariableMetadata(m_sys, "melange_back_pressure_fraction")};

    set_attrs("dimensionless pressure fraction at calving fronts due to presence of melange ", "",
              "1", "1", 0);
  }
protected:
  IceModelVec::Ptr compute_impl() const {

    IceModelVec2S::Ptr result(new IceModelVec2S(m_grid,
                                                "melange_back_pressure_fraction", WITHOUT_GHOSTS));
    result->metadata(0) = m_vars[0];

    result->copy_from(model->melange_back_pressure_fraction());

    return result;
  }
};

} // end of namespace diagnostics

DiagnosticList OceanModel::diagnostics_impl() const {
  using namespace diagnostics;
  DiagnosticList result = {
    {"sea_level",                      Diagnostic::Ptr(new PO_sea_level(this))},
    {"shelfbtemp",                     Diagnostic::Ptr(new PO_shelf_base_temperature(this))},
    {"shelfbmassflux",                 Diagnostic::Ptr(new PO_shelf_base_mass_flux(this))},
    {"melange_back_pressure_fraction", Diagnostic::Ptr(new PO_melange_back_pressure_fraction(this))}
  };

  if (m_input_model) {
    return combine(m_input_model->diagnostics(), result);
  } else {
    return result;
  }
}

TSDiagnosticList OceanModel::ts_diagnostics_impl() const {
  if (m_input_model) {
    return m_input_model->ts_diagnostics();
  } else {
    return {};
  }
}


} // end of namespace ocean
} // end of namespace pism
