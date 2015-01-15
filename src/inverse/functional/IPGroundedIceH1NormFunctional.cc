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

#include "IPGroundedIceH1NormFunctional.hh"

PetscErrorCode IPGroundedIceH1NormFunctional2S::valueAt(IceModelVec2S &x, double *OUTPUT) {

  PetscErrorCode   ierr;

  // The value of the objective
  double value = 0;

  double **x_a;
  double x_e[FEQuadrature::Nk];
  double x_q[FEQuadrature::Nq], dxdx_q[FEQuadrature::Nq], dxdy_q[FEQuadrature::Nq];
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData dirichletBC;
  ierr = dirichletBC.init(m_dirichletIndices); CHKERRQ(ierr);

  ierr = m_ice_mask.begin_access(); CHKERRQ(ierr);
  MaskQuery iceQuery(m_ice_mask);

  // Loop through all LOCAL elements.
  int xs = m_element_index.lxs, xm = m_element_index.lxm,
           ys = m_element_index.lys, ym = m_element_index.lym;
  for (int i=xs; i<xs+xm; i++) {
    for (int j=ys; j<ys+ym; j++) {
      bool all_grounded_ice = iceQuery.grounded_ice(i,j) & iceQuery.grounded_ice(i+1,j) & 
        iceQuery.grounded_ice(i,j+1) & iceQuery.grounded_ice(i+1,j+1);
      if(! all_grounded_ice) continue;
      
      m_dofmap.reset(i,j,m_grid);

      // Obtain values of x at the quadrature points for the element.
      m_dofmap.extractLocalDOFs(x_a,x_e);
      if(dirichletBC) dirichletBC.updateHomogeneous(m_dofmap,x_e);
      m_quadrature.computeTrialFunctionValues(x_e,x_q,dxdx_q,dxdy_q);

      for (int q=0; q<FEQuadrature::Nq; q++) {
        value += JxW[q]*(m_cL2*x_q[q]*x_q[q]+ m_cH1*(dxdx_q[q]*dxdx_q[q]+dxdy_q[q]*dxdy_q[q]));
      } // q
    } // j
  } // i

  ierr = PISMGlobalSum(&value, OUTPUT, m_grid.com); CHKERRQ(ierr);

  ierr = dirichletBC.finish(); CHKERRQ(ierr);

  ierr = m_ice_mask.end_access(); CHKERRQ(ierr);
  
  ierr = x.end_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode IPGroundedIceH1NormFunctional2S::dot(IceModelVec2S &a, IceModelVec2S &b, double *OUTPUT) {

  PetscErrorCode   ierr;

  // The value of the objective
  double value = 0;

  double **a_a;
  double a_e[FEQuadrature::Nk];
  double a_q[FEQuadrature::Nq], dadx_q[FEQuadrature::Nq], dady_q[FEQuadrature::Nq];
  ierr = a.get_array(a_a); CHKERRQ(ierr);

  double **b_a;
  double b_e[FEQuadrature::Nk];
  double b_q[FEQuadrature::Nq], dbdx_q[FEQuadrature::Nq], dbdy_q[FEQuadrature::Nq];
  ierr = b.get_array(b_a); CHKERRQ(ierr);

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData dirichletBC;
  ierr = dirichletBC.init(m_dirichletIndices); CHKERRQ(ierr);

  ierr = m_ice_mask.begin_access(); CHKERRQ(ierr);
  MaskQuery iceQuery(m_ice_mask);
  
  // Loop through all LOCAL elements.
  int xs = m_element_index.lxs, xm = m_element_index.lxm,
           ys = m_element_index.lys, ym = m_element_index.lym;
  for (int i=xs; i<xs+xm; i++) {
    for (int j=ys; j<ys+ym; j++) {
      bool all_grounded_ice = iceQuery.grounded_ice(i,j) & iceQuery.grounded_ice(i+1,j) & 
        iceQuery.grounded_ice(i,j+1) & iceQuery.grounded_ice(i+1,j+1);
      if(! all_grounded_ice) continue;

      m_dofmap.reset(i,j,m_grid);

      // Obtain values of x at the quadrature points for the element.
      m_dofmap.extractLocalDOFs(a_a,a_e);
      if(dirichletBC) {
        dirichletBC.updateHomogeneous(m_dofmap,a_e);
      }
      m_quadrature.computeTrialFunctionValues(a_e,a_q,dadx_q,dady_q);

      m_dofmap.extractLocalDOFs(b_a,b_e);
      if(dirichletBC) dirichletBC.updateHomogeneous(m_dofmap,b_e);
      m_quadrature.computeTrialFunctionValues(b_e,b_q,dbdx_q,dbdy_q);

      for (int q=0; q<FEQuadrature::Nq; q++) {
        value += JxW[q]*(m_cL2*a_q[q]*b_q[q]+ m_cH1*(dadx_q[q]*dbdx_q[q]+dady_q[q]*dbdy_q[q]));
      } // q
    } // j
  } // i

  ierr = PISMGlobalSum(&value, OUTPUT, m_grid.com); CHKERRQ(ierr);

  ierr = dirichletBC.finish(); CHKERRQ(ierr);

  ierr = m_ice_mask.end_access(); CHKERRQ(ierr);

  ierr = a.end_access(); CHKERRQ(ierr);
  ierr = b.end_access(); CHKERRQ(ierr);

  return 0;
}


PetscErrorCode IPGroundedIceH1NormFunctional2S::gradientAt(IceModelVec2S &x, IceModelVec2S &gradient) {

  PetscErrorCode   ierr;

  // Clear the gradient before doing anything with it!
  ierr = gradient.set(0); CHKERRQ(ierr);

  double **x_a;
  double x_e[FEQuadrature::Nk];
  double x_q[FEQuadrature::Nq], dxdx_q[FEQuadrature::Nq], dxdy_q[FEQuadrature::Nq];
  ierr = x.get_array(x_a); CHKERRQ(ierr);

  double **gradient_a;
  double gradient_e[FEQuadrature::Nk];
  ierr = gradient.get_array(gradient_a); CHKERRQ(ierr);

  // An Nq by Nk array of test function values.
  const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData dirichletBC;
  ierr = dirichletBC.init(m_dirichletIndices); CHKERRQ(ierr);

  ierr = m_ice_mask.begin_access(); CHKERRQ(ierr);
  MaskQuery iceQuery(m_ice_mask);

  // Loop through all local and ghosted elements.
  int xs = m_element_index.xs, xm = m_element_index.xm,
           ys = m_element_index.ys, ym = m_element_index.ym;
  for (int i=xs; i<xs+xm; i++) {
    for (int j=ys; j<ys+ym; j++) {
      bool all_grounded_ice = iceQuery.grounded_ice(i,j) & iceQuery.grounded_ice(i+1,j) & 
        iceQuery.grounded_ice(i,j+1) & iceQuery.grounded_ice(i+1,j+1);
      if(! all_grounded_ice) continue;

      // Reset the DOF map for this element.
      m_dofmap.reset(i,j,m_grid);

      // Obtain values of x at the quadrature points for the element.
      m_dofmap.extractLocalDOFs(i,j,x_a,x_e);
      if(dirichletBC) {
        dirichletBC.constrain(m_dofmap);
        dirichletBC.updateHomogeneous(m_dofmap,x_e);
      }
      m_quadrature.computeTrialFunctionValues(x_e,x_q,dxdx_q,dxdy_q);

      // Zero out the element-local residual in prep for updating it.
      for(int k=0;k<FEQuadrature::Nk;k++){
        gradient_e[k] = 0;
      }

      for (int q=0; q<FEQuadrature::Nq; q++) {
        const double &x_qq=x_q[q];
        const double &dxdx_qq=dxdx_q[q],  &dxdy_qq=dxdy_q[q]; 
        for(int k=0; k<FEQuadrature::Nk; k++ ) {
          gradient_e[k] += 2*JxW[q]*(m_cL2*x_qq*test[q][k].val +
            m_cH1*(dxdx_qq*test[q][k].dx + dxdy_qq*test[q][k].dy));
        } // k
      } // q
      m_dofmap.addLocalResidualBlock(gradient_e,gradient_a);
    } // j
  } // i

  ierr = dirichletBC.finish(); CHKERRQ(ierr);
  ierr = m_ice_mask.end_access(); CHKERRQ(ierr);
  ierr = x.end_access(); CHKERRQ(ierr);
  ierr = gradient.end_access(); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode IPGroundedIceH1NormFunctional2S::assemble_form(Mat form) {
  int         i,j;
  PetscErrorCode   ierr;

  // Zero out the Jacobian in preparation for updating it.
  ierr = MatZeroEntries(form);CHKERRQ(ierr);

  // Jacobian times weights for quadrature.
  double JxW[FEQuadrature::Nq];
  m_quadrature.getWeightedJacobian(JxW);

  DirichletData zeroLocs;
  ierr = zeroLocs.init(m_dirichletIndices); CHKERRQ(ierr);

  ierr = m_ice_mask.begin_access(); CHKERRQ(ierr);
  MaskQuery iceQuery(m_ice_mask);

  // Values of the finite element test functions at the quadrature points.
  // This is an Nq by Nk array of function germs (Nq=#of quad pts, Nk=#of test functions).
  const FEFunctionGerm (*test)[FEQuadrature::Nk] = m_quadrature.testFunctionValues();

  // Loop through all the elements.
  int xs = m_element_index.xs, xm = m_element_index.xm,
           ys = m_element_index.ys, ym = m_element_index.ym;
  for (i=xs; i<xs+xm; i++) {
    for (j=ys; j<ys+ym; j++) {
      bool all_grounded_ice = iceQuery.grounded_ice(i,j) & iceQuery.grounded_ice(i+1,j) & 
         iceQuery.grounded_ice(i,j+1) & iceQuery.grounded_ice(i+1,j+1);
      if(! all_grounded_ice) continue;

      // Element-local Jacobian matrix (there are FEQuadrature::Nk vector valued degrees
      // of freedom per elment, for a total of (2*FEQuadrature::Nk)*(2*FEQuadrature::Nk) = 16
      // entries in the local Jacobian.
      double      K[FEQuadrature::Nk][FEQuadrature::Nk];


      // Initialize the map from global to local degrees of freedom for this element.
      m_dofmap.reset(i,j,m_grid);

      // Don't update rows/cols where we project to zero.
      if(zeroLocs) zeroLocs.constrain(m_dofmap);

      // Build the element-local Jacobian.
      ierr = PetscMemzero(K,sizeof(K));CHKERRQ(ierr);
      for (int q=0; q<FEQuadrature::Nq; q++) {
        for (int k=0; k<4; k++) {   // Test functions
          for (int l=0; l<4; l++) { // Trial functions
            const FEFunctionGerm &test_qk=test[q][k];
            const FEFunctionGerm &test_ql=test[q][l];
            K[k][l]     += JxW[q]*(m_cL2*test_qk.val*test_ql.val
              +  m_cH1*(test_qk.dx*test_ql.dx + test_qk.dy*test_ql.dy) );
          } // l
        } // k
      } // q
      ierr = m_dofmap.addLocalJacobianBlock(&K[0][0],form);
    } // j
  } // i

  if(zeroLocs) {
    ierr = zeroLocs.fixJacobian2S(form); CHKERRQ(ierr);
  }
  ierr = zeroLocs.finish(); CHKERRQ(ierr);

  ierr = m_ice_mask.end_access(); CHKERRQ(ierr);
  
  ierr = MatAssemblyBegin(form,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(form,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  return 0;
}
