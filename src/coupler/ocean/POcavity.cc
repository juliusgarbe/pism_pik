// Copyright (C) 2012-2016 Ricarda Winkelmann, Ronja Reese, Torsten Albrecht
// and Matthias Mengel
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
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
#include <gsl/gsl_poly.h>

#include "POcavity.hh"
#include "base/util/IceGrid.hh"
#include "base/util/PISMVars.hh"
#include "base/util/iceModelVec.hh"
#include "base/util/Mask.hh"
#include "base/util/PISMConfigInterface.hh"

namespace pism {
namespace ocean {

Cavity::Constants::Constants(const Config &config) {

  numberOfBasins = 20;

  continental_shelf_depth = -800;

  T_dummy = -1.5; // standard value for ocean temperature around Antarctica (check!)
  S_dummy = 34.5; // standard value for ocean salinity around Antarctica (check!)

  earth_grav = config.get_double("constants.standard_gravity");
  rhoi       = config.get_double("constants.ice.density");
  rhow       = config.get_double("constants.sea_water.density");
  rho_star   = 1033;                  // kg/m^3
  nu         = rhoi / rho_star;       // no unit

  latentHeat = config.get_double("constants.fresh_water.latent_heat_of_fusion");
  c_p_ocean  = 3974.0;       // J/(K*kg), specific heat capacity of ocean mixed layer
  lambda     = latentHeat / c_p_ocean;   // °C, NOTE K vs °C

  a          = -0.057;       // °C/psu
  b          = 0.0832;       // °C
  c          = 7.64e-4;      // °C/dbar

  alpha      = 7.5e-5;       // 1/°C, NOTE K vs °C
  beta       = 7.7e-4;       // 1/psu

  gamma_T    = 1e-6;
  value_C    = 5e6;

  // other ice shelves
  gamma_T_o    = 1.0e-4; //config.get("gamma_T"); //1e-4;
  // m/s, thermal exchange velocity for Beckmann-Goose parameterization
  meltFactor   = 0.002;     // FIXME add to pism_config, check value
  meltSalinity = 35.0;
  b2           = 0.0939;

}

// TODO: move to POCavity.hh

const int Cavity::numberOfBoxes = 5;

const int Cavity::box_unidentified = -99;     // This should never show up in the .nc-files.
const int Cavity::box_neighboring  = -1;      // This should never show up in the .nc-files.
const int Cavity::box_noshelf      = 0;
const int Cavity::box_GL           = 1;       // ocean box covering the grounding line region
const int Cavity::box_IF           = 2;       // ocean box covering the rest of the ice shelf
const int Cavity::box_other        = 3;       // ice_shelf but there is no GL_box in the corresponding basin

const int Cavity::maskfloating = MASK_FLOATING;
const int Cavity::maskocean    = MASK_ICE_FREE_OCEAN;
const int Cavity::maskgrounded = MASK_GROUNDED;

const int Cavity::imask_inner        = 2;
const int Cavity::imask_outer        = 0;
const int Cavity::imask_exclude      = 1;
const int Cavity::imask_unidentified = -1;



Cavity::Cavity(IceGrid::ConstPtr g)
  : PGivenClimate<OceanModifier,OceanModel>(g, NULL) {
// {
//   PetscErrorCode allocate_POoceanboxmodel(); CHKERRCONTINUE(ierr);
//   if (ierr != 0)
//     PISMEnd();
// }


// void Cavity::allocate_POoceanboxmodel() {
//   PetscErrorCode ierr;

  m_option_prefix   = "-ocean_cavity";

  // will be de-allocated by the parent's destructor
  m_theta_ocean    = new IceModelVec2T;
  m_salinity_ocean = new IceModelVec2T;
  // basins = new IceModelVec2T;

  m_fields["theta_ocean"]     = m_theta_ocean;
  m_fields["salinity_ocean"]  = m_salinity_ocean;
  // m_fields["cavity_basins"]   = basins;

  process_options();

  exicerises_set = options::Bool("-exclude_icerises", "exclude ice rises in ocean cavity model");

  std::map<std::string, std::string> standard_names;
  set_vec_parameters(standard_names);

  Mx = m_grid->Mx(),
  My = m_grid->My(),
  xs = m_grid->xs(),
  xm = m_grid->xm(),
  ys = m_grid->ys(),
  ym = m_grid->xm(),
  dx = m_grid->dx(),
  dy = m_grid->dy();

  m_theta_ocean->create(m_grid, "theta_ocean");
  m_theta_ocean->set_attrs("climate_forcing",
                           "absolute potential temperature of the adjacent ocean",
                           "Kelvin", "");

  m_salinity_ocean->create(m_grid, "salinity_ocean");
  m_salinity_ocean->set_attrs("climate_forcing",
                                   "salinity of the adjacent ocean",
                                   "g/kg", "");

  m_shelfbtemp.create(m_grid, "shelfbtemp", WITHOUT_GHOSTS);
  m_shelfbtemp.set_attrs("climate_forcing",
                              "absolute temperature at ice shelf base",
                              "Kelvin", "");
  m_variables.push_back(&m_shelfbtemp);

  m_shelfbmassflux.create(m_grid, "shelfbmassflux", WITHOUT_GHOSTS);
  m_shelfbmassflux.set_attrs("climate_forcing",
                                  "ice mass flux from ice shelf base (positive flux is loss from ice shelf)",
                                  "kg m-2 s-1", "");
  m_shelfbmassflux.metadata().set_string("glaciological_units", "kg m-2 year-1");
  m_variables.push_back(&m_shelfbmassflux);

  // basins->create(m_grid, "basins");
  // basins->set_attrs("climate_forcing", "basins for ocean cavitiy model","", "");
  // m_variables.push_back(&basins);

  cbasins.create(m_grid, "basins", WITH_GHOSTS);
  cbasins.set_attrs("climate_forcing","mask determines basins for ocean cavity model",
                    "", "");
  m_variables.push_back(&cbasins);

  // mask to identify the ocean boxes
  BOXMODELmask.create(m_grid, "BOXMODELmask", WITH_GHOSTS);
  BOXMODELmask.set_attrs("model_state", "mask displaying ocean box model grid","", "");
  m_variables.push_back(&BOXMODELmask);

  // mask to identify the grounded ice rises
  ICERISESmask.create(m_grid, "ICERISESmask", WITH_GHOSTS);
  ICERISESmask.set_attrs("model_state", "mask displaying ice rises","", "");
  m_variables.push_back(&ICERISESmask);

  // mask displaying continental shelf - region where mean salinity and ocean temperature is calculated
  OCEANMEANmask.create(m_grid, "OCEANMEANmask", WITH_GHOSTS);
  OCEANMEANmask.set_attrs("model_state", "mask displaying ocean region for parameter input","", "");

  // mask with distance (in boxes) to grounding line
  DistGL.create(m_grid, "DistGL", WITH_GHOSTS);
  DistGL.set_attrs("model_state", "mask displaying distance to grounding line","", "");
  m_variables.push_back(&DistGL);

  // mask with distance (in boxes) to ice front
  DistIF.create(m_grid, "DistIF", WITH_GHOSTS);
  DistIF.set_attrs("model_state", "mask displaying distance to ice shelf calving front","", "");
  m_variables.push_back(&DistIF);

  // salinity
  Soc.create(m_grid, "Soc", WITHOUT_GHOSTS);
  Soc.set_attrs("model_state", "ocean salinity field","", "ocean salinity field");  //NOTE unit=psu
  m_variables.push_back(&Soc);

  Soc_base.create(m_grid, "Soc_base", WITHOUT_GHOSTS);
  Soc_base.set_attrs("model_state", "ocean base salinity field","", "ocean base salinity field");  //NOTE unit=psu
  m_variables.push_back(&Soc_base);

  // temperature
  Toc.create(m_grid, "Toc", WITHOUT_GHOSTS);
  Toc.set_attrs("model_state", "ocean temperature field","K", "ocean temperature field");
  m_variables.push_back(&Toc);

  Toc_base.create(m_grid, "Toc_base", WITHOUT_GHOSTS);
  Toc_base.set_attrs("model_state", "ocean base temperature","K", "ocean base temperature");
  m_variables.push_back(&Toc_base);

  Toc_inCelsius.create(m_grid, "Toc_inCelsius", WITHOUT_GHOSTS);
  Toc_inCelsius.set_attrs("model_state", "ocean box model temperature field","degree C", "ocean box model temperature field");
  m_variables.push_back(&Toc_inCelsius);

  T_star.create(m_grid, "T_star", WITHOUT_GHOSTS);
  T_star.set_attrs("model_state", "T_star field","degree C", "T_star field");
  m_variables.push_back(&T_star);

  Toc_anomaly.create(m_grid, "Toc_anomaly", WITHOUT_GHOSTS);
  Toc_anomaly.set_attrs("model_state", "ocean temperature anomaly","K", "ocean temperature anomaly");
  m_variables.push_back(&Toc_anomaly);

  overturning.create(m_grid, "overturning", WITHOUT_GHOSTS);
  overturning.set_attrs("model_state", "cavity overturning","m^3 s-1", "cavity overturning"); // no CF standard_name?
  m_variables.push_back(&overturning);

  heatflux.create(m_grid, "ocean heat flux", WITHOUT_GHOSTS);
  heatflux.set_attrs("climate_state", "ocean heat flux", "W/m^2", "");
  m_variables.push_back(&heatflux);

  basalmeltrate_shelf.create(m_grid, "basal melt rate from ocean box model", WITHOUT_GHOSTS);
  basalmeltrate_shelf.set_attrs("climate_state", "basal melt rate from ocean box model", "m/s", "");
  //FIXME unit in field is kg m-2 a-1, but the written unit is m per a
  basalmeltrate_shelf.metadata().set_string("glaciological_units", "m year-1");
  m_variables.push_back(&basalmeltrate_shelf);

  // Initialize this early so that we can check the validity of the "basins" mask read from a file
  // in Cavity::init_impl(). This number is hard-wired, so I don't think it matters that it did not
  // come from Cavity::Constants.
  numberOfBasins = 20;
}

Cavity::~Cavity() {
  // empty
}

void Cavity::init_impl() {

  m_t = m_dt = GSL_NAN;  // every re-init restarts the clock

  m_log->message(2, "* Initializing the Potsdam Cavity Model for the ocean ...\n");

  m_theta_ocean->init(m_filename, m_bc_period, m_bc_reference_time);
  m_salinity_ocean->init(m_filename, m_bc_period, m_bc_reference_time);
  // basins->init(m_filename, m_bc_period, m_bc_reference_time);

  // m_log->message(2, "a min=%f,max=%f\n",cbasins.min(),cbasins.max());

  cbasins.regrid(m_filename, CRITICAL);

  Range basins_range = cbasins.range();
  if (basins_range.min < 0 or basins_range.max > numberOfBasins - 1) {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "Some basin numbers in %s read from %s are invalid:"
                                  "allowed range is [0, %d], found [%d, %d]",
                                  cbasins.get_name().c_str(), m_filename.c_str(),
                                  numberOfBasins - 1,
                                  (int)basins_range.min, (int)basins_range.max);
  }

  m_log->message(2, "b min=%f,max=%f\n",cbasins.min(),cbasins.max());

  // read time-independent data right away:
  if (m_theta_ocean->get_n_records() == 1 &&
      m_salinity_ocean->get_n_records() == 1) {
        update(m_grid->ctx()->time()->current(), 0); // dt is irrelevant
  }

  // NOTE: moved to update_impl
  // POBMConstants cc(config);
  // initBasinsOptions(cc);

}

void Cavity::add_vars_to_output_impl(const std::string &keyword, std::set<std::string> &result) {
  PGivenClimate<OceanModifier,OceanModel>::add_vars_to_output_impl(keyword, result);

  // add variable here and in define_variables_impl
  // if you want it to appear in snapshots
  if (keyword != "none" && keyword != "small") {
    result.insert(m_shelfbtemp.get_name());
    result.insert(m_shelfbmassflux.get_name());
  }
}

void Cavity::define_variables_impl(const std::set<std::string> &vars,
                                           const PIO &nc, IO_Type nctype) {

  PGivenClimate<OceanModifier,OceanModel>::define_variables_impl(vars, nc, nctype);

  for (unsigned int k = 0; k < m_variables.size(); ++k) {
    IceModelVec *v = m_variables[k];
    std::string name = v->metadata().get_string("short_name");
    if (set_contains(vars, name)) {
      v->define(nc, nctype);
    }
  }

}

void Cavity::shelf_base_temperature_impl(IceModelVec2S &result) const {
  result.copy_from(m_shelfbtemp);
}

void Cavity::shelf_base_mass_flux_impl(IceModelVec2S &result) const {
  result.copy_from(m_shelfbmassflux);
}

void Cavity::sea_level_elevation_impl(double &result) const {
  result = m_sea_level;
}

void Cavity::melange_back_pressure_fraction_impl(IceModelVec2S &result) const {
  result.set(0.0);
}


void Cavity::write_variables_impl(const std::set<std::string> &vars, const PIO& nc) {

  PGivenClimate<OceanModifier,OceanModel>::write_variables_impl(vars, nc);

  for (unsigned int k = 0; k < m_variables.size(); ++k) {
    IceModelVec *v = m_variables[k];
    std::string name = v->metadata().get_string("short_name");
    if (set_contains(vars, name)) {
      v->write(nc);
    }
  }

}

void Cavity::initBasinsOptions(const Constants &cc) {

  m_log->message(4, "0b : set number of Basins\n");

  numberOfBasins = cc.numberOfBasins;
  numberOfBasins = options::Integer("-number_of_basins",
                                    "number of drainage basins for ocean cavity model",
                                    numberOfBasins);


  Toc_base_vec.resize(numberOfBasins);
  Soc_base_vec.resize(numberOfBasins);
  gamma_T_star_vec.resize(numberOfBasins);
  C_vec.resize(numberOfBasins);

  counter_boxes.resize(numberOfBasins, std::vector<double>(2,0)); //does this work?

  m_log->message(4, "counter_boxes(1,0) = %.2f \n", counter_boxes[1][0]);

  mean_salinity_boundary_vector.resize(numberOfBasins);
  mean_temperature_boundary_vector.resize(numberOfBasins);
  mean_meltrate_boundary_vector.resize(numberOfBasins);
  mean_overturning_GLbox_vector.resize(numberOfBasins);

  gamma_T = cc.gamma_T;
  gamma_T = options::Real("-gamma_T","gamma_T for ocean cavity model",gamma_T);

  value_C = cc.value_C;
  value_C = options::Real("-value_C","value_C for ocean cavity model",value_C);

  // data have been calculated previously for the 20 Zwally basins
  const double Toc_base_schmidtko[20] = {0.0,271.39431005,271.49081157,271.49922596,271.56714804,271.63507013,271.42228667,271.46720524,272.42253843,271.53779093,271.84942002,271.31676801,271.56846696,272.79372542,273.61694268,274.19168456,274.31958227,273.38372579,271.91951514,271.35349906}; //Schmidtko
  const double Soc_base_schmidtko[20] = {0.0,34.82193374,34.69721226,34.47641407,34.48950162,34.50258917,34.70101507,34.65306507,34.73295029,34.74859586,34.8368573,34.9529016,34.79486795,34.58380953,34.7260615,34.86198383,34.8374212 ,34.70418016,34.75598208,34.83617088}; //Schmidtko

  const double Toc_base_woa[20] = {272.99816667,271.27814004,272.1840257,272.04435251,272.20415662,272.36396072,271.48763831,271.99695864,272.06504052,272.27114732,272.66657018,271.18920729,271.74067699,273.01811291,272.15295572,273.08542047,272.74584469,273.14263356,272.58496563,272.45217911}; //World Ocean Atlas
  const double Soc_base_woa[20] = {34.6810522,34.78161073,34.67151084,34.66538478,34.67127468,34.67716458,34.75327377,34.69213327,34.72086382,34.70670158,34.71210592,34.80229468,34.76588022,34.69745763,34.7090778,34.68690903,34.66379606,34.64572337,34.6574402,34.65813983}; //World Ocean Atlas

  // std::string ocean_means;
  options::String ocean_means("-ocean_means", "TODO: description of option");

  /////////////////////////////////////////////////////////////////////////////////////

  for(int k=0;k<numberOfBasins;k++) {
      if (ocean_means.is_set()){
        if (ocean_means=="schmidtko"){
          Toc_base_vec[k] = Toc_base_schmidtko[k] - 273.15;
          Soc_base_vec[k] = Soc_base_schmidtko[k];}
        else if (ocean_means=="woa"){
          Toc_base_vec[k] = Toc_base_woa[k] - 273.15;
          Soc_base_vec[k] = Soc_base_woa[k];}
        else{
          Toc_base_vec[k] = cc.T_dummy; //dummy
          Soc_base_vec[k] = cc.S_dummy; //dummy
        }
      }
      else{
        Toc_base_vec[k] = cc.T_dummy; //dummy
        Soc_base_vec[k] = cc.S_dummy; //dummy
      }

      gamma_T_star_vec[k]= gamma_T;
      C_vec[k]           = value_C;
  }

  m_log->message(5, "     Using %d drainage basins and default values: \n"
                                "     gamma_T_star= %.2e, C = %.2e... \n"
                                 , numberOfBasins, gamma_T, value_C);

  if (not ocean_means.is_set()) {
    m_log->message(5, "  calculate Soc and Toc from thetao and salinity... \n");

    // set continental shelf depth
    continental_shelf_depth = cc.continental_shelf_depth;
    options::Real cont_shelf_depth("-continental_shelf_depth",
                                   "continental shelf depth for ocean cavity model",
                                   continental_shelf_depth);

    if (cont_shelf_depth.is_set()) {
      m_log->message(5,
      "  Depth of continental shelf for computation of temperature and salinity input\n"
      "  is set for whole domain to continental_shelf_depth=%.0f meter\n",
      continental_shelf_depth);
    }
  }

}

void Cavity::update_impl(double my_t, double my_dt) {

  // Make sure that sea water salinity and sea water potential
  // temperature fields are up to date:
  update_internal(my_t, my_dt);

  m_theta_ocean->average(m_t, m_dt);
  m_salinity_ocean->average(m_t, m_dt);

  Constants cc(*m_config);

  // FIXME: this should go to init_mpl to save cpu, but we first need to
  // make sure that the once updated basin mask is stored and not overwritten.
  round_basins();

  initBasinsOptions(cc);
  // if (omeans_set){
  //   m_log->message(4, "0c : reading mean salinity and temperatures\n");
  // } else {
    // m_log->message(4, "0c : calculating mean salinity and temperatures\n");
  identifyMASK(OCEANMEANmask,"ocean");
  computeOCEANMEANS();
  // }

  //geometry of ice shelves and temperatures
  m_log->message(4, "A  : calculating shelf_base_temperature\n");
  if (exicerises_set) {
    identifyMASK(ICERISESmask,"icerises");}
  extentOfIceShelves();
  m_log->message(2, "Back here....\n");
  identifyBOXMODELmask();
  oceanTemperature(cc);
  m_shelfbtemp.copy_from(Toc);


  //basal melt rates underneath ice shelves
  m_log->message(4, "B  : calculating shelf_base_mass_flux\n");
  basalMeltRateForGroundingLineBox(cc);
  basalMeltRateForIceFrontBox(cc); // TODO Diese Routinen woanders aufrufen (um Dopplung zu vermeiden)
  basalMeltRateForOtherShelves(cc);  //Assumes that mass flux is proportional to the shelf-base heat flux.
  //const double secpera=31556926.0;
  basalmeltrate_shelf.scale(cc.rhoi);
  m_shelfbmassflux.copy_from(basalmeltrate_shelf); //TODO Check if scaling with ice density
}

// To be used solely in round_basins()
double Cavity::most_frequent_element(const std::vector<double> &v)
  {   // Precondition: v is not empty
      std::map<double, double> frequencyMap;
      int maxFrequency = 0;
      double mostFrequentElement = 0;
      for (double x : v)
      {
          double f = ++frequencyMap[x];
          if (f > maxFrequency)
          {
              maxFrequency = f;
              mostFrequentElement = x;
          }
      }

      return mostFrequentElement;
  }

//! Round basin mask non integer values to an integral value of the next neighbor
void Cavity::round_basins() {

  //FIXME: THIS routine should be applied once in init, and roundbasins should be stored as field (assumed the basins do not change with time).

  double id_fractional;
  std::vector<double> neighbours = {0,0,0,0};

  IceModelVec::AccessList list;
  list.add(cbasins);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // do not consider domain boundaries (they should be far from the shelves.)
    if ((i==0) | (j==0) | (i>(Mx-2)) | (j>(My-2))){
      id_fractional = 0.;
    } else {
      id_fractional = (cbasins)(i,j);
      neighbours[0] = (cbasins)(i+1,j+1);
      neighbours[1] = (cbasins)(i-1,j+1);
      neighbours[2] = (cbasins)(i-1,j-1);
      neighbours[3] = (cbasins)(i+1,j-1);

      // check if this is an interpolated number:
      // first condition: not an integer
      // second condition: has no neighbour with same value
      if ((id_fractional != round(id_fractional)) ||
          ((id_fractional != neighbours[0]) &&
          (id_fractional != neighbours[1]) &&
          (id_fractional != neighbours[2]) &&
          (id_fractional != neighbours[3]))){

        double most_frequent_neighbour = most_frequent_element(neighbours);
        (cbasins)(i,j) = most_frequent_neighbour;
        // m_log->message(2, "most frequent: %f at %d,%d\n",most_frequent_neighbour,i,j);
      }
    }

  }
}

//! Identify
//!   ocean:    identify ocean up to continental shelf without detached submarine islands regions
//!   icerises: identify grounded regions without detached ice rises

void Cavity::identifyMASK(IceModelVec2S &inputmask, std::string masktype) {

  m_log->message(4, "0b1: in identifyMASK rountine\n");

  int seed_x = (Mx - 1)/2,
      seed_y = (My - 1)/2;

  double linner_identified = 0.0,
         all_inner_identified = 1.0,
         previous_step_identified = 0.0;

  const IceModelVec2CellType &m_mask = *m_grid->variables().get_2d_cell_type("mask");
  const IceModelVec2S *topg = m_grid->variables().get_2d_scalar("bedrock_altitude");

  IceModelVec::AccessList list;
  list.add(inputmask);
  list.add(m_mask);
  list.add(*topg);
  // inputmask.begin_access();

  inputmask.set(imask_unidentified);
  if ((seed_x >= xs) && (seed_x < xs+xm) && (seed_y >= ys)&& (seed_y <= ys+ym)){
    inputmask(seed_x,seed_y)=imask_inner;
  }
  // inputmask.end_access();


  // IceModelVec::AccessList list;
  // list.add(inputmask);

  int iteration_round = 0;
  // find inner region first
  while(all_inner_identified > previous_step_identified){

    iteration_round+=1;
    previous_step_identified = all_inner_identified;

    // inputmask.begin_access();
    // mask->begin_access();
    // topg->begin_access();

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

      bool masktype_condition = false;
      if (masktype=="ocean"){
        masktype_condition = (m_mask(i,j)!=maskocean || (*topg)(i,j) >= continental_shelf_depth);}
      else if (masktype=="icerises"){
        masktype_condition = (m_mask(i,j)==maskgrounded);
      }

      if (masktype_condition && inputmask(i,j)==imask_unidentified &&
        (inputmask(i,j+1)==imask_inner || inputmask(i,j-1)==imask_inner ||
         inputmask(i+1,j)==imask_inner || inputmask(i-1,j)==imask_inner)){
         inputmask(i,j)=imask_inner;
         linner_identified+=1;
      }
      else if (masktype_condition == false){
        inputmask(i,j)=imask_outer;
      }

        //m_log->message(4, "!!! %d %d, %.0f \n",i,j,inputmask(i,j));
    }

    // mask->end_access();
    // topg->end_access();

    // inputmask.end_access();
    //inputmask.beginGhostComm();
    //inputmask.endGhostComm();
    inputmask.update_ghosts();

    all_inner_identified = GlobalSum(m_grid->com, linner_identified);

  }

  // TODO: Not sure if we have to reinitialize m_mask and inputmask here.
  //set value for excluded areas (ice rises or submarine islands)
  // list.add(inputmask);
  // list.add(m_mask);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (inputmask(i,j)==imask_unidentified){
      inputmask(i,j)=imask_exclude;
    }

    if (masktype=="ocean"){ //exclude ice covered parts
      if (m_mask(i,j)!=maskocean && inputmask(i,j) == imask_inner){
        inputmask(i,j) = imask_outer;
      }
    }

  }

}


//! When ocean_given is set compute mean salinity and temperature in each basin.
void Cavity::computeOCEANMEANS() {
  // FIXME currently the mean is also calculated over submarine islands which are higher than continental_shelf_depth

  m_log->message(4, "0b2: in computeOCEANMEANS routine \n");

  std::vector<double> lm_count(numberOfBasins); //count cells to take mean over for each basin
  std::vector<double> m_count(numberOfBasins);
  std::vector<double> lm_Sval(numberOfBasins); //add salinity for each basin
  std::vector<double> lm_Tval(numberOfBasins); //add temperature for each basin
  std::vector<double> m_Tval(numberOfBasins);
  std::vector<double> m_Sval(numberOfBasins);

  for(int k=0;k<numberOfBasins;k++){
    m_count[k]=0.0;
    lm_count[k]=0.0;
    lm_Sval[k]=0.0;
    lm_Tval[k]=0.0;
    m_Tval[k]=0.0;
    m_Sval[k]=0.0;
  }

  IceModelVec::AccessList list;
  list.add(*m_theta_ocean);
  list.add(*m_salinity_ocean);
  list.add(cbasins);
  list.add(OCEANMEANmask);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (OCEANMEANmask(i,j) == imask_inner ){
      int shelf_id =(cbasins)(i,j);
      lm_count[shelf_id]+=1;
      lm_Sval[shelf_id]+=(*m_salinity_ocean)(i,j);
      lm_Tval[shelf_id]+=(*m_theta_ocean)(i,j);
    } //if

  }


  for(int k=0;k<numberOfBasins;k++) {

    m_count[k] = GlobalSum(m_grid->com, lm_count[k]);
    m_Sval[k] = GlobalSum(m_grid->com, lm_Sval[k]);
    m_Tval[k] = GlobalSum(m_grid->com, lm_Tval[k]);

    //if basin is not dummy basin 0 or there are no ocean cells in this basin to take the mean over.
    if(k>0 && m_count[k]==0){
      m_log->message(2, "PISM_WARNING: basin %d contains no ocean mean cells,"
                        " no mean salinity or temperature values are computed! \n ", k);
    } else {
      m_Sval[k] = m_Sval[k] / m_count[k];
      m_Tval[k] = m_Tval[k] / m_count[k];

      Toc_base_vec[k]=m_Tval[k] - 273.15;
      Soc_base_vec[k]=m_Sval[k];
      m_log->message(4, "  %d: temp =%.3f, salinity=%.3f\n", k, Toc_base_vec[k], Soc_base_vec[k]);
    }
  }

}


//! Compute the extent of the ice shelves of each basin/region (i.e. counter) and
//  compute for each ice shelf cell the distance to the grounding line (i.e. DistGL) and the calving front (i.e. DistIF)


void Cavity::extentOfIceShelves() {

  m_log->message(4, "A1b: in extent of ice shelves rountine\n");

  double currentLabelGL = 1; // to find DistGL, 1 if floating and directly adjacent to a grounded cell
  double currentLabelIF = 1; // to find DistIF, 1 if floating and directly adjacent to an ocean cell

  double global_continue_loop = 1;
  double local_continue_loop  = 0;

  const IceModelVec2CellType &m_mask = *m_grid->variables().get_2d_cell_type("mask");

  IceModelVec::AccessList list;
  list.add(m_mask);
  list.add(DistIF);
  list.add(cbasins);
  list.add(DistGL);

	// mask->begin_access();
	// basins->begin_access();
	// DistGL.begin_access();
	// DistIF.begin_access();

	if (exicerises_set) { list.add(ICERISESmask); }

	DistGL.set(0);
	DistIF.set(0);

	// find the grounding line and the ice front
  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

		if (m_mask(i,j)==maskfloating) { //if this is a ice shelf cell

			// label the shelf cells adjacent to the grounding line with DistGL = 1
			bool neighbor_to_land;
			if (exicerises_set) {
				neighbor_to_land = (  ICERISESmask(i,j+1)==imask_inner || ICERISESmask(i,j-1)==imask_inner ||
					ICERISESmask(i+1,j)==imask_inner || ICERISESmask(i-1,j)==imask_inner ||
 					ICERISESmask(i+1,j+1)==imask_inner || ICERISESmask(i+1,j-1)==imask_inner ||
 					ICERISESmask(i-1,j+1)==imask_inner || ICERISESmask(i-1,j-1)==imask_inner );
			} else {
				neighbor_to_land = (  m_mask(i,j+1)<maskfloating || m_mask(i,j-1)<maskfloating ||
 					m_mask(i+1,j)<maskfloating || m_mask(i-1,j)<maskfloating ||
					m_mask(i+1,j+1)<maskfloating || m_mask(i+1,j-1)<maskfloating ||
					m_mask(i-1,j+1)<maskfloating || m_mask(i-1,j-1)<maskfloating );
			}

			if (neighbor_to_land ){
				// i.e. there is a grounded neighboring cell (which is not ice rise!)
				DistGL(i,j) = currentLabelGL;
			} // no else

			// label the shelf cells adjacent to the calving front with DistIF = 1,
			// we do not need to exclude ice rises in this case.
			if (m_mask(i,j+1)==maskocean || m_mask(i,j-1)== maskocean || m_mask(i+1,j)==maskocean || m_mask(i-1,j)==maskocean ) {
				DistIF(i,j) = currentLabelIF;
			}// no else

		}
	}

	DistGL.update_ghosts();
	DistIF.update_ghosts();

  // Find DistGL for all shelf cells
  // FIXME: Do we want to take compute DistGL using four direct neigbors or
  //        also diagonal-neighbor (some points might not be reached otherwise)?

  global_continue_loop = 1;
  while( global_continue_loop !=0 ) {

    local_continue_loop = 0;

    DistGL.begin_access();

    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      if ( (m_mask)(i,j)==maskfloating && DistGL(i,j)==0 &&
        (DistGL(i,j+1)==currentLabelGL || DistGL(i,j-1)==currentLabelGL ||
        DistGL(i+1,j)==currentLabelGL || DistGL(i-1,j)==currentLabelGL // ||
        //DistGL(i+1,j+1)==currentLabelGL || DistGL(i+1,j-1)==currentLabelGL ||
        //DistGL(i-1,j+1)==currentLabelGL || DistGL(i-1,j-1)==currentLabelGL
        ) ) { // i.e. this is an shelf cell with no distance assigned yet and with a neighbor that has a distance assigned
          DistGL(i,j) = currentLabelGL+1;
          local_continue_loop = 1;
      } //if

    } // for

    currentLabelGL++;
    DistGL.end_access();
    DistGL.update_ghosts();

    global_continue_loop = GlobalMax(m_grid->com, local_continue_loop);

  } // while: find DistGL

  // Find DistIF for all shelf cells
  // FIXME: Do we want to take compute DistIF using four direct neigbors or
  //        also diagonal-neighbor (some points might not be reached otherwise)?


  global_continue_loop = 1; // start loop
  while( global_continue_loop !=0  ) {

    local_continue_loop = 0;

    DistIF.begin_access();

    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      if ( m_mask(i,j)==maskfloating && DistIF(i,j)==0 &&
        (DistIF(i,j+1)==currentLabelIF || DistIF(i,j-1)==currentLabelIF ||
        DistIF(i+1,j)==currentLabelIF || DistIF(i-1,j)==currentLabelIF // ||
        //DistIF(i+1,j+1)==currentLabelIF || DistIF(i+1,j-1)==currentLabelIF ||
        //DistIF(i-1,j+1)==currentLabelIF || DistIF(i-1,j-1)==currentLabelIF
        ) ) { // i.e. this is an shelf cell with no distance assigned yet and with a neighbor that has a distance assigned
          DistIF(i,j)=currentLabelIF+1;
          local_continue_loop = 1;
      } //if

    } // for


    currentLabelIF++;
    DistIF.end_access();
    DistIF.update_ghosts();

    global_continue_loop = GlobalMax(m_grid->com, local_continue_loop);

  } // while: find DistIF

}


//! Compute the BOXMODELmask based on DistGL and DistIF, calculate the extent of each box in each region

void Cavity::identifyBOXMODELmask() {

  m_log->message(2, "A1c: in identify boxmodel mask rountine\n");

  // Find the maximal DistGL and DistIF
  // FIXME! this could already be done in routine where DistGL and DistIF are computed
  std::vector<double> max_distGL(numberOfBasins);
  std::vector<double> max_distIF(numberOfBasins);
  std::vector<double> lmax_distGL(numberOfBasins);
  std::vector<double> lmax_distIF(numberOfBasins);

  const IceModelVec2CellType &m_mask = *m_grid->variables().get_2d_cell_type("mask");

  for(int k=0;k<numberOfBasins;k++){ max_distGL[k]=0.0; max_distIF[k]=0.0;lmax_distGL[k]=0.0; lmax_distIF[k]=0.0;}

  IceModelVec::AccessList list;
  list.add(cbasins);
  list.add(DistGL);
  list.add(DistIF);
  list.add(BOXMODELmask);
  list.add(m_mask);

  for (Points p(*m_grid); p; p.next()) {
  const int i = p.i(), j = p.j();
    int shelf_id = (cbasins)(i,j);

    if ( DistGL(i,j)> lmax_distGL[shelf_id] ) {
      lmax_distGL[shelf_id] = DistGL(i,j);
    } //if
    if ( DistIF(i,j)> lmax_distIF[shelf_id] ) {
      lmax_distIF[shelf_id] = DistIF(i,j);
    } //if
  } // for


  for (int l=0;l<numberOfBasins;l++){
    max_distGL[l] = GlobalMax(m_grid->com, lmax_distGL[l]);
    max_distIF[l] = GlobalMax(m_grid->com, lmax_distIF[l]);
  }



  // Define the number of boxes for each basin
  std::vector<int> lnumberOfBoxes_perBasin(numberOfBasins);

  int n_min = 1; //
  double max_distGL_ref = 500000; // meter
  double zeta = 0.5;
  // numberOfBoxes = 5; // FIXME Do we want this to be a chosable parameter?

  for (int l=0;l<numberOfBasins;l++){
    lnumberOfBoxes_perBasin[l] = 0;
    //ATTENTION, this is only correct for same dx and dy spacing.
    // Otherwise, we need to change the calculation of DistGL and DistIF
    lnumberOfBoxes_perBasin[l] = n_min + static_cast<int>(
        round(pow((max_distGL[l]*dx/max_distGL_ref), zeta) *(numberOfBoxes-n_min)));
    m_log->message(2, "lnumberOfBoxes[%d]=%d \n", l, lnumberOfBoxes_perBasin[l]);
  }

  // Define the BOXMODELmask

  // IceModelVec::AccessList list;
  // list.add(cbasins);
  // list.add(DistGL);
  // list.add(DistIF);


  BOXMODELmask.set(0);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    if (m_mask(i,j)==maskfloating && DistGL(i,j)>0 && DistIF(i,j)>0 && BOXMODELmask(i,j)==0){
      int shelf_id = (cbasins)(i,j);
      int n = lnumberOfBoxes_perBasin[shelf_id];
      double r = DistGL(i,j)*1.0/(DistGL(i,j)*1.0+DistIF(i,j)*1.0); // relative distance between grounding line and ice front

      for(int k=0;k<n;++k){

        // First variant to define the BOXMODELmask using a rule like k/n< (1-r)**2 <k+1/n
        // this rule is motivated by splitting a half-circle into halfcircles of same area and using 1-r like some kind of radius

        if (  ((n*1.0-k*1.0-1.0)/(n*1.0) <= pow((1.0-r),2)) && (pow((1.0-r), 2) <= (n*1.0-k*1.0)/n*1.0) ){ // FIXME do we need to multiply by 1.0 here?
          if (DistGL(i,j) < k+1) {
            BOXMODELmask(i,j) = DistGL(i,j); // the boxnumber of a cell cannot be bigger then the distance to the grounding line //FIXME Discuss!!!
          } else{
          BOXMODELmask(i,j) = k+1;
          }
        }//if
        /*
        // Second variant to define the BOXMODELmask using a rule like k/n < r**0.5 < k+1/n
        if (  ((k*1.0)/(n*1.0) <= pow(r,0.5)) && (pow(r, 0.5) <= (k*1.0+1.0)/n*1.0) ){ // FIXME do we need to multiply by 1.0 here?
          if (DistGL(i,j) < k+1) {
            BOXMODELmask(i,j) = DistGL(i,j); // the boxnumber of a cell cannot be bigger then the distance to the grounding line //FIXME Discuss!!!
          } else{
          BOXMODELmask(i,j) = k+1;
          }
        }//if */

      } //for
    }
  } // for


  // set all floating cells which have no BOXMODELmask value as numberOfBoxes+1 -> beckmann-goose for melting
  // those are the cells which are not reachable from GL or IF //FIXME does that make sense?

  // BOXMODELmask.begin_access();
  // mask->begin_access();



  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    if (m_mask(i,j)==maskfloating && BOXMODELmask(i,j)==0){ // floating
      BOXMODELmask(i,j) = numberOfBoxes + 1;
    }

  }

  // BOXMODELmask.end_access();
  // mask->end_access();


  //m_log->message(2, "Number of Boxes=%.0f, lnumberOfBoxes = %.0f\n", numberOfBoxes, lnumberOfBoxes);

  // Compute the number of cells per box and basin. Later: Include this in the loop above to save time...
  const int nBoxes = numberOfBoxes+2;
  std::vector<std::vector<int> > lcounter_boxes(
    numberOfBasins, std::vector<int>(nBoxes));
  for (int k=0;k<numberOfBasins;k++){
    for (int l=0;l<nBoxes;l++){
      lcounter_boxes[k][l]=0;
    }
  }

  // BOXMODELmask.begin_access();
  // basins->begin_access();


  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();
    int box_id = static_cast<int>(round(BOXMODELmask(i,j)));
    if (box_id > 0){ // floating
      int shelf_id = (cbasins)(i,j);
      lcounter_boxes[shelf_id][box_id]++;
    }
  }

  // BOXMODELmask.end_access();
  // basins->end_access();

  for (int k=0;k<numberOfBasins;k++){
    counter_boxes[k].resize(nBoxes);
    for (int l=0;l<nBoxes;l++){
      counter_boxes[k][l] = GlobalSum(m_grid->com, lcounter_boxes[k][l]);
    }
  }

}



/*!
Compute ocean temperature outside of the ice shelf cavities.
*/


void Cavity::oceanTemperature(const Constants &cc) {

  m_log->message(4, "A2 : in ocean temp rountine\n");

  const IceModelVec2S *ice_thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");
  const IceModelVec2CellType &m_mask = *m_grid->variables().get_2d_cell_type("mask");

  IceModelVec::AccessList list;
  list.add(*ice_thickness);
  list.add(cbasins);
  list.add(Soc_base);
  list.add(Toc_base);
  list.add(Toc_anomaly);
  list.add(Toc);
  list.add(m_mask);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    // make sure all temperatures are zero at the beginning of each timestep
    Toc(i,j) = 273.15; // in K
    Toc_base(i,j) = 273.15;  // in K
    Toc_anomaly(i,j) = 0.0;  // in K or °C
    Soc_base(i,j) = 0.0; // in psu


    if (m_mask(i,j)==maskfloating){
      int shelf_id = (cbasins)(i,j);
      Toc_base(i,j) = 273.15 + Toc_base_vec[shelf_id];
      Soc_base(i,j) =  Soc_base_vec[shelf_id];

      //! salinity and temperature for grounding line box
      if ( Soc_base(i,j) == 0.0 || Toc_base_vec[shelf_id] == 0.0 ) {
        throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                      "PISM_ERROR: Missing Soc_base and Toc_base for"
                                      "%d, %d, basin %d \n   Aborting... \n", i, j, shelf_id);
      }


      // Add temperature anomalies from given nc-file
      // FIXME different nc-files for each basin!
      if ((delta_T != NULL) && ocean_oceanboxmodel_deltaT_set) {
        //Toc_anomaly(i,j) = delta_T_factor * (*delta_T)(m_t + 0.5*m_dt);
        Toc_anomaly(i,j) = delta_T_factor * temp_anomaly;

      } else {

        Toc_anomaly(i,j) = 0.0;
      }

      ////////////////////////////////////////////////////////////////////////////////////////////////////
      //prevent ocean temp from being below pressure melting temperature

      //const double shelfbaseelev = - (cc.rhoi / cc.rhow) * (*ice_thickness)(i,j);

      const double pressure = cc.rhoi * cc.earth_grav * (*ice_thickness)(i,j) * 1e-4; // MUST be in dbar  // NOTE 1dbar = 10000 Pa = 1e4 kg m-1 s-2,
      const double T_pmt = cc.a*Soc_base(i,j) + cc.b - cc.c*pressure;

      //m_log->message(5, "!!!!! T_pmt=%f, Ta=%f, Tb=%f, Toc=%f, Ta2=%f, Toc2=%f at %d,%d,%d\n", T_pmt,   Toc_anomaly(i,j),   Toc_base(i,j)-273.15,   Toc_base(i,j)-273.15 + Toc_anomaly(i,j),    PetscMax( T_pmt+273.15-Toc_base(i,j),   Toc_anomaly(i,j)),    Toc_base(i,j)-273.15+PetscMax( T_pmt+273.15-Toc_base(i,j),Toc_anomaly(i,j))  ,i  ,j,  shelf_id);

      Toc_anomaly(i,j) = PetscMax( T_pmt + 273.15 - Toc_base(i,j) , Toc_anomaly(i,j));
      /////////////////////////////////////////////////////////////////////////////////////////////////////

      Toc(i,j) = Toc_base(i,j) + Toc_anomaly(i,j); // in K


    } // end if herefloating
  } // end i

}



// NOTE Mean Gl_box meltrate is needed for basalMeltRateForIceFrontBox(). Here, mean is taken over all shelves for each basin!

//! Compute the basal melt / refreezing rates for each shelf cell bordering the grounding line box
void Cavity::basalMeltRateForGroundingLineBox(const Constants &cc) {

  m_log->message(4, "B1 : in basal melt rate gl rountine\n");

  std::vector<double> lcounter_edge_of_GLbox_vector(numberOfBasins);
  std::vector<double> lmean_salinity_GLbox_vector(numberOfBasins);
  std::vector<double> lmean_temperature_GLbox_vector(numberOfBasins);
  std::vector<double> lmean_meltrate_GLbox_vector(numberOfBasins);
  std::vector<double> lmean_overturning_GLbox_vector(numberOfBasins);

  for (int k=0;k<numberOfBasins;k++){
    lcounter_edge_of_GLbox_vector[k]=0.0;
    lmean_salinity_GLbox_vector[k]=0.0;
    lmean_temperature_GLbox_vector[k]=0.0;
    lmean_meltrate_GLbox_vector[k]=0.0;
    lmean_overturning_GLbox_vector[k]=0.0;
  }

  const IceModelVec2S *ice_thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  IceModelVec::AccessList list;
  list.add(*ice_thickness);
  list.add(cbasins);
  list.add(BOXMODELmask);
  list.add(T_star);
  list.add(Toc_base);
  list.add(Toc_anomaly);
  list.add(Toc_inCelsius);
  list.add(Toc);
  list.add(Soc_base);
  list.add(Soc);
  list.add(overturning);
  list.add(basalmeltrate_shelf);


  double countHelpterm=0,
         lcountHelpterm=0;


  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    int shelf_id = (cbasins)(i,j);

    // Make sure everything is at default values at the beginning of each timestep
    T_star(i,j) = 0.0; // in °C
    Toc_inCelsius(i,j) = 0.0; // in °C
    Soc(i,j) = 0.0; // in psu

    basalmeltrate_shelf(i,j) = 0.0;
    overturning(i,j) = 0.0;


    if (BOXMODELmask(i,j) == box_GL && shelf_id > 0.0){

      const double pressure = cc.rhoi * cc.earth_grav * (*ice_thickness)(i,j) * 1e-4; // MUST be in dbar  // NOTE 1dbar = 10000 Pa = 1e4 kg m-1 s-2
      // FIXME need to include atmospheric pressure?
      T_star(i,j) = cc.a*Soc_base(i,j) + cc.b - cc.c*pressure - (Toc_base(i,j) - 273.15 + Toc_anomaly(i,j)); // in °C

      double gamma_T_star,C1,g1;

      gamma_T_star = gamma_T_star_vec[shelf_id];
      C1 = C_vec[shelf_id];
      g1 = (counter_boxes[shelf_id][box_GL] * dx * dy) * gamma_T_star / (C1*cc.rho_star); //NEW TEST


      //! temperature for grounding line box

      double helpterm1 = g1/(cc.beta*(Soc_base(i,j) / (cc.nu*cc.lambda)) - cc.alpha);                  // in 1 / (1/°C) = °C
      double helpterm2 = (g1*T_star(i,j)) / (cc.beta*(Soc_base(i,j) / (cc.nu*cc.lambda)) - cc.alpha); // in °C / (1/°C) = °C^2


      if ((0.25*PetscSqr(helpterm1) -helpterm2) < 0.0) {
        //m_log->message(5, "PISM_ERROR: Tb=%f, Ta=%f, Tst=%f, Sb=%f  at %d, %d\n\n",Toc_base(i,j),Toc_anomaly(i,j),T_star(i,j),Soc_base(i,j),i,j);
        //m_log->message(5, "PISM_ERROR: h1=%f, h2=%f, h1sq=%f at %d, %d\n\n",helpterm1,helpterm2,0.25*PetscSqr(helpterm1),i,j);
        //m_log->message(5, "PISM_ERROR: square-root is negative! %f at %d, %d\n...with 0.25*helpterm^2=%f,helpterm2=%f,g1=%f,(cc.beta*(Soc_base(i,j)/(cc.nu*cc.lambda))-cc.alpha)=%f,Tstar=%f\n Not aborting, but setting sum to 0... \n", 0.25*PetscSqr(helpterm1) -helpterm2, i, j, 0.25*PetscSqr(helpterm1),helpterm2,g1,(cc.beta*(Soc_base(i,j) / (cc.nu*cc.lambda)) - cc.alpha),T_star(i,j));
        //PISMEnd();
        helpterm2=0.25*PetscSqr(helpterm1);
        //FIXME: In this case, there is no solution for the basal melt rate, how to deal with these cells?
        lcountHelpterm+=1;
      }

      // NOTE Careful, Toc_base(i,j) is in K, Toc_inCelsius(i,j) NEEDS to be in °C!
      Toc_inCelsius(i,j) = (Toc_base(i,j)-273.15+Toc_anomaly(i,j)) - ( -0.5*helpterm1 + sqrt(0.25*PetscSqr(helpterm1) -helpterm2) );

      //! salinity for grounding line box
      Soc(i,j) = Soc_base(i,j) - (Soc_base(i,j) / (cc.nu*cc.lambda)) * ((Toc_base(i,j)-273.15+Toc_anomaly(i,j)) - Toc_inCelsius(i,j));  // in psu

      //! basal melt rate for grounding line box
      basalmeltrate_shelf(i,j) = (-gamma_T_star/(cc.nu*cc.lambda)) * (cc.a*Soc(i,j) + cc.b - cc.c*pressure - Toc_inCelsius(i,j));  // in m/s

      //! overturning
      // NOTE Actually, there is of course no overturning-FIELD, it is only a scalar for each shelf.
      // Here, we compute overturning as   MEAN[C1*cc.rho_star* (cc.beta*(Soc_base(i,j)-Soc(i,j)) - cc.alpha*((Toc_base(i,j)-273.15+Toc_anomaly(i,j))-Toc_inCelsius(i,j)))]
      // while in fact it should be   C1*cc.rho_star* (cc.beta*(Soc_base-MEAN[Soc(i,j)]) - cc.alpha*((Toc_base-273.15+Toc_anomaly)-MEAN[Toc_inCelsius(i,j)]))
      // which is the SAME since Soc_base, Toc_base and Toc_anomaly are the same FOR ALL i,j CONSIDERED, so this is just nomenclature!
      overturning(i,j) = C1*cc.rho_star* (cc.beta*(Soc_base(i,j)-Soc(i,j)) - cc.alpha*((Toc_base(i,j)-273.15+Toc_anomaly(i,j))-Toc_inCelsius(i,j))); // in m^3/s

      // box_IF ist irreleitend, gemeint is die neben der GLbox
      if (BOXMODELmask(i-1,j)==box_IF || BOXMODELmask(i+1,j)==box_IF || BOXMODELmask(i,j-1)==box_IF || BOXMODELmask(i,j+1)==box_IF){
      // i.e., if this cell is from the GL box and one of the neighbours is from the CF box - It is important to only take the border of the grounding line box
      // to the calving front box into account, because the following mean value will be used to compute the value for the calving front box. I.e., this helps avoiding discontinuities!
        lcounter_edge_of_GLbox_vector[shelf_id]++;
        lmean_salinity_GLbox_vector[shelf_id] += Soc(i,j);
        lmean_temperature_GLbox_vector[shelf_id] += Toc_inCelsius(i,j);
        lmean_meltrate_GLbox_vector[shelf_id] += basalmeltrate_shelf(i,j);
        lmean_overturning_GLbox_vector[shelf_id] += overturning(i,j);

        //m_log->message(4, "B1 : in basal melt rate gl rountine test1: %d,%d, %d: %.0f \n",i,j,shelf_id,lcounter_edge_of_GLbox_vector[shelf_id]);

      } // no else-case necessary since all variables are set to zero at the beginning of this routine

    }else { // i.e., not GL_box
        basalmeltrate_shelf(i,j) = 0.0;
    }
  }

  for(int k=0;k<numberOfBasins;k++) {
    double counter_edge_of_GLbox_vector=0.0;

    counter_edge_of_GLbox_vector = GlobalSum(m_grid->com, lcounter_edge_of_GLbox_vector[k]);
    mean_meltrate_boundary_vector[k] = GlobalSum(m_grid->com, lmean_meltrate_GLbox_vector[k]);
    mean_salinity_boundary_vector[k] = GlobalSum(m_grid->com, lmean_salinity_GLbox_vector[k]);
    mean_temperature_boundary_vector[k] = GlobalSum(m_grid->com, lmean_temperature_GLbox_vector[k]);
    mean_overturning_GLbox_vector[k] = GlobalSum(m_grid->com, lmean_overturning_GLbox_vector[k]);


    if (counter_edge_of_GLbox_vector>0.0){
      mean_salinity_boundary_vector[k] = mean_salinity_boundary_vector[k]/counter_edge_of_GLbox_vector;
      mean_temperature_boundary_vector[k] = mean_temperature_boundary_vector[k]/counter_edge_of_GLbox_vector;
      mean_meltrate_boundary_vector[k] = mean_meltrate_boundary_vector[k]/counter_edge_of_GLbox_vector;
      mean_overturning_GLbox_vector[k] = mean_overturning_GLbox_vector[k]/counter_edge_of_GLbox_vector;
    } else { // This means that there is no [cell from the GLbox neighboring a cell from the CFbox], NOT necessarily that there is no GLbox!
      mean_salinity_boundary_vector[k]=0.0; mean_temperature_boundary_vector[k]=0.0; mean_meltrate_boundary_vector[k]=0.0; mean_overturning_GLbox_vector[k]=0.0;
    }

    m_log->message(2, "  %d: cnt=%.0f, sal=%.3f, temp=%.3f, melt=%.3e, over=%.1e \n", k,counter_edge_of_GLbox_vector,mean_salinity_boundary_vector[k],mean_temperature_boundary_vector[k],mean_meltrate_boundary_vector[k],mean_overturning_GLbox_vector[k]) ;
  }

    countHelpterm = GlobalSum(m_grid->com, lcountHelpterm);
    if (countHelpterm > 0) {
      m_log->message(2, "B1!: PISM_WARNING: square-root has been negative in %.0f cases!\n",
        countHelpterm);
    }

}



// NEW Routine to compute bmr
// !! all other boxes
//! Compute the basal melt / refreezing rates for each shelf cell bordering the ice front box

void Cavity::basalMeltRateForIceFrontBox(const Constants &cc) { //FIXME rename routine!!

  m_log->message(4, "B2 : in bm other shelves rountine\n");

  double countk4=0,
         lcountk4=0,
         countGl0=0,
         lcountGl0=0,
         countSqr=0,
         lcountSqr=0,
         countMean0=0,
         lcountMean0=0;

  int nBoxes = static_cast<int>(round(numberOfBoxes+1)); // do not include the Beckmann-Goose Box!

  const IceModelVec2S *ice_thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  //! Iterate over all Boxes > 1=GF_Box
  for (int iBox=2; iBox <nBoxes; ++iBox) {
    m_log->message(2, "B2 : iBox =%d, numberOfBoxes=%d \n", iBox, numberOfBoxes);


    std::vector<double> lcounter_edge_of_ibox_vector(numberOfBasins);     // to compute means at boundary for the current box
    std::vector<double> lmean_salinity_ibox_vector(numberOfBasins);
    std::vector<double> lmean_temperature_ibox_vector(numberOfBasins);
    std::vector<double> lmean_meltrate_ibox_vector(numberOfBasins);

    for (int k=0;k<numberOfBasins;k++){
      lcounter_edge_of_ibox_vector[k]=0.0;
      lmean_salinity_ibox_vector[k]=0.0;
      lmean_temperature_ibox_vector[k]=0.0;
      lmean_meltrate_ibox_vector[k]=0.0;
    }

    // TODO: does this need to be within the loop over boxes?
    // TODO: do we really need all these variables as full fields?
    IceModelVec::AccessList list;
    list.add(*ice_thickness);
    list.add(cbasins);
    list.add(BOXMODELmask);
    list.add(T_star);
    list.add(Toc_base);
    list.add(Toc_anomaly);
    list.add(Toc_inCelsius);
    list.add(Toc);
    list.add(Soc_base);
    list.add(Soc);
    list.add(overturning);
    list.add(basalmeltrate_shelf);

    // for iBox compute the melt rates.

    for (Points p(*m_grid); p; p.next()) {
      const int i = p.i(), j = p.j();

      int shelf_id = (cbasins)(i,j);

      if (BOXMODELmask(i,j)==iBox && shelf_id > 0.0){

        const double pressure = cc.rhoi * cc.earth_grav * (*ice_thickness)(i,j) * 1e-4; // MUST be in dbar  // NOTE 1dbar = 10000 Pa = 1e4 kg m-1 s-2
        T_star(i,j) = cc.a*Soc_base(i,j) + cc.b - cc.c*pressure - (Toc_base(i,j) - 273.15 + Toc_anomaly(i,j)); // in °C

        double  gamma_T_star,area_iBox,mean_salinity_in_boundary,mean_temperature_in_boundary,mean_meltrate_in_boundary,mean_overturning_in_GLbox;

        gamma_T_star = gamma_T_star_vec[shelf_id];
        area_iBox = (counter_boxes[shelf_id][iBox] * dx * dy);

        // FIXME RENAME THESE in GENERAL
        mean_salinity_in_boundary = mean_salinity_boundary_vector[shelf_id];
        mean_temperature_in_boundary = mean_temperature_boundary_vector[shelf_id]; // note: in degree Celsius, mean over Toc_inCelsius
        mean_meltrate_in_boundary = mean_meltrate_boundary_vector[shelf_id];
        mean_overturning_in_GLbox = mean_overturning_GLbox_vector[shelf_id]; // !!!leave this one with the grounding line box


        if (mean_salinity_in_boundary==0 || mean_overturning_in_GLbox ==0) { // if there are no boundary values from the box before
          // This should not happen any more since we use distIF and distGL, so every cell within a OBM-Box has to be reachable from IF and GL
          //m_log->message(5, "!!!! GLBOX =0 , basin = %d at %d,%d, \n   ", shelf_id,i,j);
          m_log->message(2, "!!!! ATTENTION, this should not happen(?) by the definition of the boxes, problem at %d,%d \n", i,j);
          BOXMODELmask(i,j) = numberOfBoxes+1;
          lcountGl0+=1;

        } else {
          // compute melt rates with OBM

          double k1,k2,k3,k4,k5;

          k1 = (area_iBox*gamma_T_star);
          // in (m^2*m/s)= m^3/s
          k2 = (mean_overturning_in_GLbox + area_iBox*gamma_T_star);
          // in m^3/s
          if (k2==0){
            throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                          "PISM_ERROR: Division by zero! k2=%f at %d, %d\n   "
                                          "Aborting... \n", k2, i, j);
          }
          k3 = (k1/(cc.nu*cc.lambda)*cc.a - k1*k1/(cc.nu*cc.lambda*k2)*cc.a);
          // in m^3/(s*°C)*°C/psu - m^6/(s^2*°C*m^3/s)*°C/psu = m^3/(s*psu)
          k4 = (-mean_overturning_in_GLbox + k1/(cc.nu*cc.lambda)*cc.b - k1/(cc.nu*cc.lambda)*cc.c*pressure - k1/(cc.nu*cc.lambda)*mean_overturning_in_GLbox/k2*mean_temperature_in_boundary - k1*k1/(cc.nu*cc.lambda*k2)*cc.b + k1*k1/(cc.nu*cc.lambda*k2)*cc.c*pressure);
          // in m^3/s
          k5 = mean_overturning_in_GLbox*mean_salinity_in_boundary;
          // m^3/s*psu

          //m_log->message(2, grid.com,"!!!! ACF=%.3e, AGL=%.3e, MS=%.3f, MM=%.3f, MO=%.3f, basin = %d at %d,%d, \n   ",  area_CFbox,area_GLbox,mean_salinity_in_boundary,mean_meltrate_in_boundary,mean_overturning_in_GLbox,shelf_id,i,j);
          //m_log->message(2, grid.com,"!!!! k1=%.3f, k2=%.3f, k3=%.3f, k4=%.3f, k5=%.3f, k6=%.3f, basin = %d at %d,%d, \n   ",k1,k2,k3,k4,k5,k6,shelf_id,i,j);

          //! salinity for calving front box
          if (k3 == 0.0) {
            //m_log->message(5, grid.com,"PISM_ERROR: Division by zero! k3=%f at %d, %d\n   Aborting... \n", k3, i, j);
            //m_log->message(5, grid.com,"PISM_ERROR: Probably mean_overturning_in_GLbox = %f is zero, check if there is a grounding line box in basin %d , \n   ", mean_overturning_in_GLbox, shelf_id);
            //PISMEnd();
            // In this case, there is no solution for the melt rates, we compute melt rates following Beckmann-Goose
            lcountk4+=1;
            BOXMODELmask(i,j) = numberOfBoxes+1;
            continue;
          }

          if ((0.25*k4*k4/(k3*k3) -k5/k3) < 0.0) {
            // In this case, there is no solution for the melt rates, we compute melt rates following Beckmann-Goose
            //m_log->message(5, grid.com,"PISM_ERROR: Square-root is negative! %f at %d, %d\n...with 0.25*k5^2/k3^2 - k4/k3 =%f \n   Aborting... \n", (0.25*k5*k5/(k3*k3) -k4/k3)) ;
            //PISMEnd();
            lcountSqr+=1;
            BOXMODELmask(i,j) = numberOfBoxes+1;
            continue;
          }

          // salinity for calving front box
          Soc(i,j) = - 0.5*k4/k3 + (sqrt(0.25*k4*k4/(k3*k3) - k5/k3) ); // in psu // Plus or minus???

          //! temperature for calving front box
          // NOTE Careful, Toc_base(i,j) is in K, Toc_inCelsius(i,j) NEEDS to be in °C!
          Toc_inCelsius(i,j) = 1/k2 *(mean_overturning_in_GLbox*mean_temperature_in_boundary + area_iBox*gamma_T_star*(cc.a*Soc(i,j) + cc.b - cc.c*pressure));

          //! basal melt rate for calving front box
          basalmeltrate_shelf(i,j) = (-gamma_T_star/(cc.nu*cc.lambda)) * (cc.a*Soc(i,j) + cc.b - cc.c*pressure - Toc_inCelsius(i,j)); // in m/s

          if (mean_salinity_in_boundary == 0.0 || mean_temperature_in_boundary == 0.0 || mean_meltrate_in_boundary == 0.0 || mean_overturning_in_GLbox == 0.0){
            // NEW: THIS SHOULD NOT HAPPEN ANY MORE, since every cell can be reached from GL (distGL taken into account)
            // In this case, there is no solution for the melt rates, we compute melt rates following Beckmann-Goose
            // this must not occur since there must always be a GL_box neighbor
            //m_log->message(5, grid.com, "PISM_ERROR: DETECTION CFBOX: There is no neighbouring grounding line box for this calving front box at %d,%d! \nThis will lead to a zero k4 and in turn to NaN in Soc, Toc_inCelsius and basalmeltrate_shelf. After the next massContExplicitStep(), H will be NaN, too! This will cause ks in temperatureStep() to be NaN and lead to a Segmentation Violation! \nIn particular: basin_id=%d, BOXMODELmask=%f, H=%f, T_star=%f, \narea_GLbox=%e, area_CFbox=%e, mean_salinity_in_GLbox=%f, mean_meltrate_in_GLbox=%e, mean_overturning_in_GLbox=%e, \nk1=%e,k2=%e,k3=%e,k4=%e,k5=%e,k6=%e, \nToc_base=%f, Toc_anomaly=%f, Toc_inCelsius=%f, Toc=%f, Soc_base=%f, Soc=%f, basalmeltrate_shelf=%e \n   Aborting... \n", i,j, shelf_id, BOXMODELmask(i,j), (*ice_thickness)(i,j), T_star(i,j), area_GLbox,area_CFbox,mean_salinity_in_GLbox,mean_meltrate_in_GLbox,mean_overturning_in_GLbox,k1,k2,k3,k4,k5,k6, Toc_base(i,j), Toc_anomaly(i,j), Toc_inCelsius(i,j), Toc(i,j), Soc_base(i,j), Soc(i,j), basalmeltrate_shelf(i,j));
            //PISMEnd();
            lcountMean0+=1;
            BOXMODELmask(i,j) = numberOfBoxes+1;
            continue;
          }
          // compute means at boundary to next box
          if (BOXMODELmask(i-1,j)==(iBox+1) || BOXMODELmask(i+1,j)==(iBox+1) || BOXMODELmask(i,j-1)==(iBox+1) || BOXMODELmask(i,j+1)==(iBox+1)){
            // i.e., if this cell is from the current Box and one of the neighbours is from the next higher box - It is important to only take the border of the current box
            // to the calving front box into account, because the following mean value will be used to compute the value for the calving front box. I.e., this helps avoiding discontinuities!
            lcounter_edge_of_ibox_vector[shelf_id]++;
            lmean_salinity_ibox_vector[shelf_id] += Soc(i,j);
            lmean_temperature_ibox_vector[shelf_id] += Toc_inCelsius(i,j);
            lmean_meltrate_ibox_vector[shelf_id] += basalmeltrate_shelf(i,j);
            //m_log->message(4, grid.com,"B1 : in basal melt rate gl rountine test1: %d,%d, %d: %.0f \n",i,j,shelf_id,lcounter_edge_of_GLbox_vector[shelf_id]);
          } // no else-case necessary since all variables are set to zero at the beginning of this routine
        }
      } // NOTE NO else-case, since  basalMeltRateForGroundingLineBox() and basalMeltRateForOtherShelves() cover all other cases and we would overwrite those results here.
    }

    for(int k=0;k<numberOfBasins;k++) {
      // NOTE: overturning should not be changed!!!
      double counter_edge_of_ibox_vector=0.0;
      counter_edge_of_ibox_vector = GlobalSum(m_grid->com, lcounter_edge_of_ibox_vector[k]);
      mean_meltrate_boundary_vector[k] = GlobalSum(m_grid->com, lmean_meltrate_ibox_vector[k]);
      mean_salinity_boundary_vector[k] = GlobalSum(m_grid->com, lmean_salinity_ibox_vector[k]);
      mean_temperature_boundary_vector[k] = GlobalSum(m_grid->com, lmean_temperature_ibox_vector[k]);

      if (counter_edge_of_ibox_vector>0.0){
        mean_salinity_boundary_vector[k] = mean_salinity_boundary_vector[k]/counter_edge_of_ibox_vector;
        mean_temperature_boundary_vector[k] = mean_temperature_boundary_vector[k]/counter_edge_of_ibox_vector;
        mean_meltrate_boundary_vector[k] = mean_meltrate_boundary_vector[k]/counter_edge_of_ibox_vector;
      } else { // This means that there is no [cell from the GLbox neighboring a cell from the CFbox], NOT necessarily that there is no GLbox!
        mean_salinity_boundary_vector[k]=0.0; mean_temperature_boundary_vector[k]=0.0; mean_meltrate_boundary_vector[k]=0.0;
      }

      m_log->message(2, "  %d: cnt=%.0f, sal=%.3f, temp=%.3f, melt=%.3e, over=%.1e \n", k,counter_edge_of_ibox_vector,mean_salinity_boundary_vector[k],mean_temperature_boundary_vector[k],mean_meltrate_boundary_vector[k],mean_overturning_GLbox_vector[k]) ;
    } // basins

  } // iBox

  // FIXME is das der richtige Ort?
  countk4 = GlobalSum(m_grid->com, lcountk4);
  countGl0 = GlobalSum(m_grid->com, lcountGl0);
  countSqr = GlobalSum(m_grid->com, lcountSqr);
  countMean0 = GlobalSum(m_grid->com, lcountMean0);

  if (countk4 > 0) {
    m_log->message(2, "B2!: PISM_WARNING: k4 is zero in %.0f case(s)!\n",countk4);
  }
  if (countGl0 > 0) {
    m_log->message(2, "B2!: PISM_WARNING: no grounding line box in basin in %.0f case(s)!\n",countGl0);
  }
  if (countSqr > 0) {
    m_log->message(2, "B2!: PISM_WARNING: square root is negative in %.0f case(s)!\n",countSqr);
  }
  if (countMean0 > 0) {
    m_log->message(2, "B2!: PISM_WARNING: mean of salinity, meltrate or overturning is zero in %.0f case(s)!\n",countMean0);
  }

}



// END OF NEW




//! Convert Toc_inCelsius from °C to K and write into Toc for the .nc-file; NOTE It is crucial, that Toc_inCelsius is in °C for the computation of the basal melt rate
//! Compute the melt rate for all other ice shelves.
void Cavity::basalMeltRateForOtherShelves(const Constants &cc) {

  m_log->message(4, "B3 : in bm others rountine\n");


  const IceModelVec2S *ice_thickness = m_grid->variables().get_2d_scalar("land_ice_thickness");

  // TODO: do we really need all these variables as full fields?
  IceModelVec::AccessList list;
  list.add(*ice_thickness);
  list.add(cbasins);
  list.add(BOXMODELmask);
  list.add(Toc_base);
  list.add(Toc_anomaly);
  list.add(Toc_inCelsius);
  list.add(Toc);
  list.add(overturning);
  list.add(basalmeltrate_shelf);  // NOTE meltrate has units:   J m-2 s-1 / (J kg-1 * kg m-3) = m s-1
  list.add(heatflux);

  for (Points p(*m_grid); p; p.next()) {
    const int i = p.i(), j = p.j();

    int shelf_id = (cbasins)(i,j);

    if (shelf_id == 0) { // boundary of computational domain

      basalmeltrate_shelf(i,j) = 0.0;

    } else if (BOXMODELmask(i,j)==(numberOfBoxes+1) ) {

      Toc(i,j) = Toc_base(i,j) + Toc_anomaly(i,j); // in K, NOTE: Toc_base is already in K, so no (+273.15)
      // default: compute the melt rate from the temperature field according to beckmann_goosse03 (see below)


      const double shelfbaseelev = - (cc.rhoi / cc.rhow) * (*ice_thickness)(i,j);


      //FIXME: for consistency reasons there should be constants a,b,c, gamma_T used
      double T_f = 273.15 + (cc.a*cc.meltSalinity + cc.b2 + cc.c*shelfbaseelev); // add 273.15 to get it in Kelvin... 35 is the salinity

      heatflux(i,j) = cc.meltFactor * cc.rhow * cc.c_p_ocean * cc.gamma_T_o * (Toc(i,j) - T_f);  // in W/m^2
      basalmeltrate_shelf(i,j) = heatflux(i,j) / (cc.latentHeat * cc.rhoi); // in m s-1

    } else if (shelf_id > 0.0) {
      // Note: Here Toc field is set for all (!) floating grid cells, it is not set (and does not appear) in the routines before.
      Toc(i,j) = 273.15 + Toc_inCelsius(i,j) + Toc_anomaly(i,j); // in K
    } else { // This must not happen

      throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                    "PISM_ERROR: [rank %d] at %d, %d  -- basins(i,j)=%d causes problems.\n"
                                    "Aborting... \n",m_grid->rank(), i, j, shelf_id);
    }
  }

}

} // end of namespace ocean
} // end of namespace pism
