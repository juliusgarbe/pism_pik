// Copyright (C) 2013, 2014  David Maxwell
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

#include "IPLogRatioFunctional.hh"

//! Determine the normalization constant for the functional.
/*! Sets the normalization constant \f$c_N\f$ so that
\f[
J(x)=1
\f]
if  \f$|x| = \mathtt{scale}|u_{\rm obs}| \f$ everywhere.
*/
PetscErrorCode IPLogRatioFunctional::normalize(double scale) {
  PetscErrorCode   ierr;

  double value = 0;

  double **w_a;
  double w = 1.;

  PISMVector2 **u_obs_a;
  ierr = m_u_observed.get_array(u_obs_a); CHKERRQ(ierr);

  if(m_weights){
    ierr = m_weights->get_array(w_a); CHKERRQ(ierr);
  }
  for( int i=m_grid.xs; i<m_grid.xs+m_grid.xm; i++) {
    for( int j=m_grid.ys; j<m_grid.ys+m_grid.ym; j++) {
      if( m_weights ) {
        w = w_a[i][j];
      }

      PISMVector2 &u_obs_ij = u_obs_a[i][j];
      double obsMagSq = u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v + m_eps*m_eps;

      double modelMagSq = scale*scale*(u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v) + m_eps*m_eps;
      
      double v = log( modelMagSq/obsMagSq);
      value += w*v*v;
    }
  }
  if(m_weights) {
    ierr = m_weights->end_access(); CHKERRQ(ierr);
  }
  ierr = m_u_observed.end_access(); CHKERRQ(ierr);

  ierr = PISMGlobalSum(&value, &m_normalization, m_grid.com); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IPLogRatioFunctional::valueAt(IceModelVec2V &x, double *OUTPUT)  {
  PetscErrorCode   ierr;

  // The value of the objective
  double value = 0;

  double **w_a;
  double w = 1.;

  PISMVector2 **x_a;
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  PISMVector2 **u_obs_a;
  ierr = m_u_observed.get_array(u_obs_a); CHKERRQ(ierr);
  if(m_weights){
    ierr = m_weights->get_array(w_a); CHKERRQ(ierr);
  }
  for( int i=m_grid.xs; i<m_grid.xs+m_grid.xm; i++) {
    for( int j=m_grid.ys; j<m_grid.ys+m_grid.ym; j++) {
      if( m_weights ) {
        w = w_a[i][j];
      }
      PISMVector2 &x_ij = x_a[i][j];
      PISMVector2 &u_obs_ij = u_obs_a[i][j];
      PISMVector2 u_model_ij = x_ij+u_obs_ij;
      double obsMagSq = u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v + m_eps*m_eps;

      double modelMagSq = (u_model_ij.u*u_model_ij.u + u_model_ij.v*u_model_ij.v)+m_eps*m_eps;
      double v = log( modelMagSq/obsMagSq);
      value += w*v*v;
    }
  }
  if(m_weights) {
    ierr = m_weights->end_access(); CHKERRQ(ierr);
  }
  ierr = m_u_observed.end_access(); CHKERRQ(ierr);

  value /= m_normalization;
  
  ierr = PISMGlobalSum(&value, OUTPUT, m_grid.com); CHKERRQ(ierr);

  ierr = x.end_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IPLogRatioFunctional::gradientAt(IceModelVec2V &x, IceModelVec2V &gradient)  {
  PetscErrorCode   ierr;

  gradient.set(0);

  double **w_a;
  double w = 1.;

  PISMVector2 **x_a;
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  PISMVector2 **gradient_a;
  ierr = gradient.get_array(gradient_a); CHKERRQ(ierr);

  PISMVector2 **u_obs_a;
  ierr = m_u_observed.get_array(u_obs_a); CHKERRQ(ierr);
  if(m_weights){
    ierr = m_weights->get_array(w_a); CHKERRQ(ierr);
  }
  for( int i=m_grid.xs; i<m_grid.xs+m_grid.xm; i++) {
    for( int j=m_grid.ys; j<m_grid.ys+m_grid.ym; j++) {
      if( m_weights ) {
        w = w_a[i][j];
      }
      PISMVector2 &x_ij = x_a[i][j];
      PISMVector2 &u_obs_ij = u_obs_a[i][j];
      PISMVector2 u_model_ij = x_ij+u_obs_ij;

      double obsMagSq = u_obs_ij.u*u_obs_ij.u + u_obs_ij.v*u_obs_ij.v + m_eps*m_eps;
      double modelMagSq = (u_model_ij.u*u_model_ij.u + u_model_ij.v*u_model_ij.v)+m_eps*m_eps;
      double v = log( modelMagSq/obsMagSq);
      double dJdw =  2*w*v/modelMagSq;

      gradient_a[i][j].u = dJdw*2*u_model_ij.u/m_normalization;
      gradient_a[i][j].v = dJdw*2*u_model_ij.v/m_normalization;
    }
  }
  ierr = m_u_observed.end_access(); CHKERRQ(ierr);
  if(m_weights) {
    ierr = m_weights->end_access(); CHKERRQ(ierr);
  }

  ierr = x.end_access(); CHKERRQ(ierr);
  ierr = gradient.end_access(); CHKERRQ(ierr);

  return 0;
}
