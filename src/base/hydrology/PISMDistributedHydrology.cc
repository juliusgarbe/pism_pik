// Copyright (C) 2012-2014 PISM Authors
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

#include "PISMHydrology.hh"
#include "hydrology_diagnostics.hh"
#include "PISMVars.hh"
#include "pism_options.hh"
#include "Mask.hh"
#include "PISMStressBalance.hh"


PISMDistributedHydrology::PISMDistributedHydrology(IceGrid &g, const PISMConfig &conf,
                                                   PISMStressBalance *sb)
    : PISMRoutingHydrology(g, conf)
{
    stressbalance = sb;
    if (allocate_pressure() != 0) {
      PetscPrintf(grid.com,
        "PISM ERROR: memory allocation failed in PISMDistributedHydrology constructor (pressure).\n");
      PISMEnd();
    }
}

PISMDistributedHydrology::~PISMDistributedHydrology() {
  // empty
}

PetscErrorCode PISMDistributedHydrology::allocate_pressure() {
  PetscErrorCode ierr;

  // additional variables beyond PISMRoutingHydrology::allocate()
  ierr = P.create(grid, "bwp", WITH_GHOSTS, 1); CHKERRQ(ierr);
  ierr = P.set_attrs("model_state",
                     "pressure of transportable water in subglacial layer",
                     "Pa", ""); CHKERRQ(ierr);
  P.metadata().set_double("valid_min", 0.0);
  ierr = cbase.create(grid, "ice_sliding_speed", WITHOUT_GHOSTS); CHKERRQ(ierr);
  ierr = cbase.set_attrs("internal",
                         "ice sliding speed seen by subglacial hydrology",
                         "m s-1", ""); CHKERRQ(ierr);
  cbase.metadata().set_double("valid_min", 0.0);
  ierr = Pnew.create(grid, "Pnew_internal", WITHOUT_GHOSTS); CHKERRQ(ierr);
  ierr = Pnew.set_attrs("internal",
                     "new transportable subglacial water pressure during update",
                     "Pa", ""); CHKERRQ(ierr);
  Pnew.metadata().set_double("valid_min", 0.0);
  ierr = psi.create(grid, "hydraulic_potential", WITH_GHOSTS, 1); CHKERRQ(ierr);
  ierr = psi.set_attrs("internal",
                       "hydraulic potential of water in subglacial layer",
                       "Pa", ""); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode PISMDistributedHydrology::init(PISMVars &vars) {
  PetscErrorCode ierr;
  ierr = verbPrintf(2, grid.com,
    "* Initializing the distributed, linked-cavities subglacial hydrology model...\n");
    CHKERRQ(ierr);

  bool init_P_from_steady = false, strip_set = false;
  ierr = PetscOptionsBegin(grid.com, "",
            "Options controlling the 'distributed' subglacial hydrology model", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsIsSet("-report_mass_accounting",
      "Report to stdout on mass accounting in hydrology models",
                            report_mass_accounting); CHKERRQ(ierr);

    stripwidth = grid.convert(stripwidth, "m", "km");
    ierr = PISMOptionsReal("-hydrology_null_strip",
                           "set the width, in km, of the strip around the edge "
                           "of the computational domain in which hydrology is inactivated",
                           stripwidth, strip_set); CHKERRQ(ierr);
    if (strip_set == true) {
      stripwidth = grid.convert(stripwidth, "km", "m");
    }
    ierr = PISMOptionsIsSet("-init_P_from_steady",
                            "initialize P from formula P(W) which applies in steady state",
                            init_P_from_steady); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  ierr = PISMHydrology::init(vars); CHKERRQ(ierr);

  ierr = PISMRoutingHydrology::init_bwat(vars); CHKERRQ(ierr);

  ierr = init_bwp(vars); CHKERRQ(ierr);

  if (init_P_from_steady) { // if so, just overwrite -i or -bootstrap value of P=bwp
    ierr = verbPrintf(2, grid.com,
            "  option -init_P_from_steady seen ...\n"
            "  initializing P from P(W) formula which applies in steady state\n"); CHKERRQ(ierr);
    ierr = P_from_W_steady(P); CHKERRQ(ierr);
  }

  return 0;
}


PetscErrorCode PISMDistributedHydrology::init_bwp(PISMVars &vars) {
  PetscErrorCode ierr;

  // initialize water layer thickness from the context if present,
  //   otherwise from -i or -boot_file, otherwise with constant value
  bool i_set = false, bootstrap_set = false;
  ierr = PetscOptionsBegin(grid.com, "",
            "Options for initializing bwp in the 'distributed' subglacial hydrology model", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsIsSet("-i", "PISM input file", i_set); CHKERRQ(ierr);
    ierr = PISMOptionsIsSet("-boot_file", "PISM bootstrapping file",
                            bootstrap_set); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  // initialize P: present or -i file or -bootstrap file or set to constant;
  //   then overwrite by regrid; then overwrite by -init_P_from_steady
  const double bwp_default = config.get("bootstrapping_bwp_value_no_var");
  IceModelVec2S *P_input = dynamic_cast<IceModelVec2S*>(vars.get("bwp"));

  if (P_input != NULL) { // a variable called "bwp" is already in context
    ierr = P.copy_from(*P_input); CHKERRQ(ierr);
  } else if (i_set || bootstrap_set) {
    std::string filename;
    int start;
    ierr = find_pism_input(filename, bootstrap_set, start); CHKERRQ(ierr);
    if (i_set) {
      bool bwp_exists = false;
      PIO nc(grid, "guess_mode");
      ierr = nc.open(filename, PISM_NOWRITE); CHKERRQ(ierr);
      ierr = nc.inq_var("bwp", bwp_exists); CHKERRQ(ierr);
      ierr = nc.close(); CHKERRQ(ierr);
      if (bwp_exists == true) {
        ierr = P.read(filename, start); CHKERRQ(ierr);
      } else {
        ierr = verbPrintf(2, grid.com,
                          "PISM WARNING: bwp for hydrology model not found in '%s'."
                          "  Setting it to %.2f ...\n",
                          filename.c_str(), bwp_default); CHKERRQ(ierr);
        ierr = P.set(bwp_default); CHKERRQ(ierr);
      }
    } else {
      ierr = P.regrid(filename, OPTIONAL, bwp_default); CHKERRQ(ierr);
    }
  } else {
    ierr = P.set(bwp_default); CHKERRQ(ierr);
  }

  ierr = regrid("PISMDistributedHydrology", &P); CHKERRQ(ierr); //  we could be asked to regrid from file
  return 0;
}


void PISMDistributedHydrology::add_vars_to_output(std::string keyword, std::set<std::string> &result) {
  PISMRoutingHydrology::add_vars_to_output(keyword, result);
  result.insert("bwp");
}


PetscErrorCode PISMDistributedHydrology::define_variables(std::set<std::string> vars, const PIO &nc,
                                                 PISM_IO_Type nctype) {
  PetscErrorCode ierr;
  ierr = PISMRoutingHydrology::define_variables(vars, nc, nctype); CHKERRQ(ierr);
  if (set_contains(vars, "bwp")) {
    ierr = P.define(nc, nctype); CHKERRQ(ierr);
  }
  return 0;
}


PetscErrorCode PISMDistributedHydrology::write_variables(std::set<std::string> vars, const PIO &nc) {
  PetscErrorCode ierr;
  ierr = PISMRoutingHydrology::write_variables(vars, nc); CHKERRQ(ierr);
  if (set_contains(vars, "bwp")) {
    ierr = P.write(nc); CHKERRQ(ierr);
  }
  return 0;
}


void PISMDistributedHydrology::get_diagnostics(std::map<std::string, PISMDiagnostic*> &dict,
                                               std::map<std::string, PISMTSDiagnostic*> &/*ts_dict*/) {
  // bwat is state
  // bwp is state
  dict["bwprel"] = new PISMHydrology_bwprel(this, grid, *variables);
  dict["effbwp"] = new PISMHydrology_effbwp(this, grid, *variables);
  dict["hydroinput"] = new PISMHydrology_hydroinput(this, grid, *variables);
  dict["wallmelt"] = new PISMHydrology_wallmelt(this, grid, *variables);
  dict["bwatvel"] = new PISMRoutingHydrology_bwatvel(this, grid, *variables);
}


//! Copies the P state variable which is the modeled water pressure.
PetscErrorCode PISMDistributedHydrology::subglacial_water_pressure(IceModelVec2S &result) {
  PetscErrorCode ierr;
  ierr = P.copy_to(result); CHKERRQ(ierr);
  return 0;
}


//! Check bounds on P and fail with message if not satisfied.  Optionally, enforces the upper bound instead of checking it.
/*!
The bounds are \f$0 \le P \le P_o\f$ where \f$P_o\f$ is the overburden pressure.
 */
PetscErrorCode PISMDistributedHydrology::check_P_bounds(bool enforce_upper) {
  PetscErrorCode ierr;

  ierr = overburden_pressure(Pover); CHKERRQ(ierr);

  ierr = P.begin_access(); CHKERRQ(ierr);
  ierr = Pover.begin_access(); CHKERRQ(ierr);
  for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
      if (P(i,j) < 0.0) {
        PetscPrintf(grid.com,
           "PISM ERROR: disallowed negative subglacial water pressure\n"
           "    P = %.6f Pa\n at (i,j)=(%d,%d)\n"
           "ENDING ... \n\n", P(i,j),i,j);
        PISMEnd();
      }
      if (enforce_upper) {
        P(i,j) = PetscMin(P(i,j), Pover(i,j));
      } else if (P(i,j) > Pover(i,j) + 0.001) {
        PetscPrintf(grid.com,
           "PISM ERROR: subglacial water pressure P = %.16f Pa exceeds\n"
           "    overburden pressure Po = %.16f Pa at (i,j)=(%d,%d)\n"
           "ENDING ... \n\n", P(i,j),Pover(i,j),i,j);
        PISMEnd();
      }
    }
  }
  ierr = P.end_access(); CHKERRQ(ierr);
  ierr = Pover.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Compute functional relationship P(W) which applies only in steady state.
/*!
In steady state in this model, water pressure is determined by a balance of
cavitation (opening) caused by sliding and creep closure.

This will be used in initialization when P is otherwise unknown, and
in verification and/or reporting.  It is not used during time-dependent
model runs.  To be more complete, \f$P=P(W,P_o,|v_b|)\f$.
 */
PetscErrorCode PISMDistributedHydrology::P_from_W_steady(IceModelVec2S &result) {
  PetscErrorCode ierr;
  double CC = config.get("hydrology_cavitation_opening_coefficient") /
                    (config.get("hydrology_creep_closure_coefficient") * config.get("ice_softness")),
    powglen = 1.0 / config.get("Glen_exponent"),
    Wr = config.get("hydrology_roughness_scale");

  ierr = overburden_pressure(Pover); CHKERRQ(ierr);

  ierr = W.begin_access();      CHKERRQ(ierr);
  ierr = Pover.begin_access();  CHKERRQ(ierr);
  ierr = cbase.begin_access();  CHKERRQ(ierr);
  ierr = result.begin_access(); CHKERRQ(ierr);
  for (int i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (int j = grid.ys; j < grid.ys + grid.ym; ++j) {
      double sb = pow(CC * cbase(i, j), powglen);
      if (W(i, j) == 0.0) {
        // see P(W) formula in steady state; note P(W) is continuous (in steady
        // state); these facts imply:
        if (sb > 0.0)
          result(i, j) = 0.0;        // no water + cavitation = underpressure
        else
          result(i, j) = Pover(i, j); // no water + no cavitation = creep repressurizes = overburden
      } else {
        double Wratio = PetscMax(0.0, Wr - W(i, j)) / W(i, j);
        // in cases where steady state is actually possible this will
        //   come out positive, but otherwise we should get underpressure P=0,
        //   and that is what it yields
        result(i, j) = PetscMax(0.0, Pover(i, j) - sb * pow(Wratio, powglen));
      }
    }
  }
  ierr = W.end_access();      CHKERRQ(ierr);
  ierr = Pover.end_access();  CHKERRQ(ierr);
  ierr = cbase.end_access();  CHKERRQ(ierr);
  ierr = result.end_access(); CHKERRQ(ierr);
  return 0;
}


//! Update the the sliding speed |v_b| from ice quantities.
/*!
Calls a PISMStressBalance method to get the vector basal velocity of the ice,
and then computes the magnitude of that.
 */
PetscErrorCode PISMDistributedHydrology::update_cbase(IceModelVec2S &result_cbase) {
  PetscErrorCode ierr;
  IceModelVec2V* Ubase; // ice sliding velocity
  // cbase = |v_b|
  ierr = stressbalance->get_2D_advective_velocity(Ubase); CHKERRQ(ierr);
  ierr = Ubase->magnitude(result_cbase); CHKERRQ(ierr);
  return 0;
}


//! Computes the adaptive time step for this (W,P) state space model.
PetscErrorCode PISMDistributedHydrology::adaptive_for_WandP_evolution(
                  double t_current, double t_end, double maxKW,
                  double &dt_result,
                  double &maxV_result, double &maxD_result,
                  double &PtoCFLratio) {
  PetscErrorCode ierr;
  double dtCFL, dtDIFFW, dtDIFFP;

  ierr = adaptive_for_W_evolution(t_current,t_end, maxKW,
              dt_result,maxV_result,maxD_result,dtCFL,dtDIFFW); CHKERRQ(ierr);

  const double phi0 = config.get("hydrology_regularizing_porosity");
  dtDIFFP = 2.0 * phi0 * dtDIFFW;

  // dt = min([te-t dtmax dtCFL dtDIFFW dtDIFFP]);
  dt_result = PetscMin(dt_result, dtDIFFP);

  if (dtDIFFP > 0.0)
    PtoCFLratio = PetscMax(1.0, dtCFL / dtDIFFP);
  else
    PtoCFLratio = 1.0;

  ierr = verbPrintf(3,grid.com,
                    "   [%.5e  %.7f  %.6f  %.9f  -->  dt = %.9f (a)  at  t = %.6f (a)]\n",
                    grid.convert(maxV_result, "m/second", "m/year"),
                    grid.convert(dtCFL,       "seconds",  "years"),
                    grid.convert(dtDIFFW,     "seconds",  "years"),
                    grid.convert(dtDIFFP,     "seconds",  "years"),
                    grid.convert(dt_result,   "seconds",  "years"),
                    grid.convert(t_current,   "seconds",  "years")); CHKERRQ(ierr);
  return 0;
}


//! Update the model state variables W,P by running the subglacial hydrology model.
/*!
Runs the hydrology model from time icet to time icet + icedt.  Here [icet,icedt]
is generally on the order of months to years.  This hydrology model will take its
own shorter time steps, perhaps hours to weeks.
 */
PetscErrorCode PISMDistributedHydrology::update(double icet, double icedt) {
  PetscErrorCode ierr;

  // if asked for the identical time interval versus last time, then
  //   do nothing; otherwise assume that [my_t,my_t+my_dt] is the time
  //   interval on which we are solving
  if ((fabs(icet - m_t) < 1e-12) && (fabs(icedt - m_dt) < 1e-12))
    return 0;
  // update PISMComponent times: t = current time, t+dt = target time
  m_t = icet;
  m_dt = icedt;

  // make sure W,P have valid ghosts before starting hydrology steps
  ierr = W.update_ghosts(); CHKERRQ(ierr);
  ierr = P.update_ghosts(); CHKERRQ(ierr);

  // from current ice geometry/velocity variables, initialize Po and cbase
  ierr = update_cbase(cbase); CHKERRQ(ierr);

  const double
            rg    = config.get("fresh_water_density") * config.get("standard_gravity"),
            nglen = config.get("Glen_exponent"),
            Aglen = config.get("ice_softness"),
            c1    = config.get("hydrology_cavitation_opening_coefficient"),
            c2    = config.get("hydrology_creep_closure_coefficient"),
            Wr    = config.get("hydrology_roughness_scale"),
            phi0  = config.get("hydrology_regularizing_porosity");

  double ht = m_t, hdt = 0, // hydrology model time and time step
            maxKW = 0, maxV = 0, maxD = 0;
  double icefreelost = 0, oceanlost = 0, negativegain = 0, nullstriplost = 0,
            delta_icefree = 0, delta_ocean = 0, delta_neggain = 0, delta_nullstrip = 0;

  double PtoCFLratio = 0,  // for reporting ratio of dtCFL to dtDIFFP
            cumratio = 0.0;
  int hydrocount = 0; // count hydrology time steps

  while (ht < m_t + m_dt) {
    hydrocount++;

#if (PISM_DEBUG==1)
    ierr = check_water_thickness_nonnegative(W); CHKERRQ(ierr);
    ierr = check_Wtil_bounds(); CHKERRQ(ierr);
#endif

    // update Wtilnew (the actual step) from W and Wtil
    ierr = raw_update_Wtil(hdt); CHKERRQ(ierr);
    ierr = boundary_mass_changes(Wtilnew, delta_icefree, delta_ocean,
                                 delta_neggain, delta_nullstrip); CHKERRQ(ierr);
    icefreelost  += delta_icefree;
    oceanlost    += delta_ocean;
    negativegain += delta_neggain;
    nullstriplost+= delta_nullstrip;

    // note that ice dynamics can change overburden pressure, so we can only check P
    //   bounds if thk has not changed; if thk could have just changed, such as in the
    //   first time through the current loop, we enforce them
    ierr = check_P_bounds((hydrocount == 1)); CHKERRQ(ierr);

    ierr = water_thickness_staggered(Wstag); CHKERRQ(ierr);
    ierr = Wstag.update_ghosts(); CHKERRQ(ierr);

    ierr = conductivity_staggered(Kstag,maxKW); CHKERRQ(ierr);
    ierr = Kstag.update_ghosts(); CHKERRQ(ierr);

    ierr = velocity_staggered(V); CHKERRQ(ierr);

    // to get Qstag, W needs valid ghosts
    ierr = advective_fluxes(Qstag); CHKERRQ(ierr);
    ierr = Qstag.update_ghosts(); CHKERRQ(ierr);

    ierr = adaptive_for_WandP_evolution(ht, m_t+m_dt, maxKW, hdt, maxV, maxD, PtoCFLratio); CHKERRQ(ierr);
    cumratio += PtoCFLratio;

    if ((inputtobed != NULL) || (hydrocount==1)) {
      ierr = get_input_rate(ht,hdt,total_input); CHKERRQ(ierr);
    }

    // update Pnew from time step
    const double  CC = (rg * hdt) / phi0,
                     wux  = 1.0 / (grid.dx * grid.dx),
                     wuy  = 1.0 / (grid.dy * grid.dy);
    double  Open, Close, divflux, ZZ,
               divadflux, diffW;
    ierr = overburden_pressure(Pover); CHKERRQ(ierr);

    MaskQuery M(*mask);

    ierr = P.begin_access(); CHKERRQ(ierr);
    ierr = W.begin_access(); CHKERRQ(ierr);
    ierr = Wtil.begin_access(); CHKERRQ(ierr);
    ierr = Wtilnew.begin_access(); CHKERRQ(ierr);
    ierr = cbase.begin_access(); CHKERRQ(ierr);
    ierr = Wstag.begin_access(); CHKERRQ(ierr);
    ierr = Kstag.begin_access(); CHKERRQ(ierr);
    ierr = Qstag.begin_access(); CHKERRQ(ierr);
    ierr = total_input.begin_access(); CHKERRQ(ierr);
    ierr = mask->begin_access(); CHKERRQ(ierr);
    ierr = Pover.begin_access(); CHKERRQ(ierr);
    ierr = Pnew.begin_access(); CHKERRQ(ierr);
    for (int i=grid.xs; i<grid.xs+grid.xm; ++i) {
      for (int j=grid.ys; j<grid.ys+grid.ym; ++j) {
        if (M.ice_free_land(i,j))
          Pnew(i,j) = 0.0;
        else if (M.ocean(i,j))
          Pnew(i,j) = Pover(i,j);
        else if (W(i,j) <= 0.0) {
          // see P(W) formula *in steady state*; note P(W) is continuous (in steady
          // state); these facts imply:
          if (cbase(i,j) > 0.0)
            Pnew(i,j) = 0.0;        // no water + cavitation = underpressure
          else
            Pnew(i,j) = Pover(i,j); // no water + no cavitation = creep repressurizes = overburden
        } else {
          // opening and closure terms in pressure equation
          Open = PetscMax(0.0,c1 * cbase(i,j) * (Wr - W(i,j)));
          Close = c2 * Aglen * pow(Pover(i,j) - P(i,j),nglen) * W(i,j);

          // compute the flux divergence the same way as in raw_update_W()
          divadflux =   (Qstag(i,j,0) - Qstag(i-1,j  ,0)) / grid.dx
                      + (Qstag(i,j,1) - Qstag(i,  j-1,1)) / grid.dy;
          const double  De = rg * Kstag(i,  j,0) * Wstag(i,  j,0),
                           Dw = rg * Kstag(i-1,j,0) * Wstag(i-1,j,0),
                           Dn = rg * Kstag(i,j  ,1) * Wstag(i,j  ,1),
                           Ds = rg * Kstag(i,j-1,1) * Wstag(i,j-1,1);
          diffW =   wux * (  De * (W(i+1,j) - W(i,j)) - Dw * (W(i,j) - W(i-1,j)) )
                  + wuy * (  Dn * (W(i,j+1) - W(i,j)) - Ds * (W(i,j) - W(i,j-1)) );
          divflux = - divadflux + diffW;

          // pressure update equation
          ZZ = Close - Open + total_input(i,j) - (Wtilnew(i,j) - Wtil(i,j)) / hdt;
          Pnew(i,j) = P(i,j) + CC * ( divflux + ZZ );
          // projection to enforce  0 <= P <= P_o
          Pnew(i,j) = PetscMin(PetscMax(0.0, Pnew(i,j)), Pover(i,j));
        }
      }
    }
    ierr = P.end_access(); CHKERRQ(ierr);
    ierr = W.end_access(); CHKERRQ(ierr);
    ierr = Wtil.end_access(); CHKERRQ(ierr);
    ierr = Wtilnew.end_access(); CHKERRQ(ierr);
    ierr = cbase.end_access(); CHKERRQ(ierr);
    ierr = Pnew.end_access(); CHKERRQ(ierr);
    ierr = Pover.end_access(); CHKERRQ(ierr);
    ierr = total_input.end_access(); CHKERRQ(ierr);
    ierr = Wstag.end_access(); CHKERRQ(ierr);
    ierr = Kstag.end_access(); CHKERRQ(ierr);
    ierr = Qstag.end_access(); CHKERRQ(ierr);
    ierr = mask->end_access(); CHKERRQ(ierr);

    // FIXME: following chunk is code duplication with PISMRoutingHydrology::update()

    // update Wnew (the actual step) from W, Wtil, Wtilnew, Wstag, Qstag, total_input
    ierr = raw_update_W(hdt); CHKERRQ(ierr);
    ierr = boundary_mass_changes(Wnew, delta_icefree, delta_ocean,
                                 delta_neggain, delta_nullstrip); CHKERRQ(ierr);
    icefreelost  += delta_icefree;
    oceanlost    += delta_ocean;
    negativegain += delta_neggain;
    nullstriplost+= delta_nullstrip;

    // transfer new into old
    ierr = Wnew.update_ghosts(W); CHKERRQ(ierr);
    ierr = Wtilnew.copy_to(Wtil); CHKERRQ(ierr);
    ierr = Pnew.update_ghosts(P); CHKERRQ(ierr);

    ht += hdt;
  } // end of hydrology model time-stepping loop

  if (report_mass_accounting) {
    ierr = verbPrintf(2, grid.com,
                      " 'distributed' hydrology summary:\n"
                      "     %d hydrology sub-steps with average dt = %.7f years = %.2f s\n"
                      "        (average of %.2f steps per CFL time; max |V| = %.2e m s-1; max D = %.2e m^2 s-1)\n"
                      "     ice free land lost = %.3e kg, ocean lost = %.3e kg\n"
                      "     negative bmelt gain = %.3e kg, null strip lost = %.3e kg\n",
                      hydrocount, grid.convert(m_dt/hydrocount, "seconds", "years"), m_dt/hydrocount,
                      cumratio/hydrocount, maxV, maxD,
                      icefreelost, oceanlost,
                      negativegain, nullstriplost); CHKERRQ(ierr);
  }
  return 0;
}
