///////////////////////////////////////////////////////////////////////////////
//
// File: ForcingMovingBody.h
//
// For more information, please see: http://www.nektar.info
//
// The MIT License
//
// Copyright (c) 2006 Division of Applied Mathematics, Brown University (USA),
// Department of Aeronautics, Imperial College London (UK), and Scientific
// Computing and Imaging Institute, University of Utah (USA).
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
// Description: Moving Body (Wavyness and acceleration)
//
///////////////////////////////////////////////////////////////////////////////

#ifndef NEKTAR_SOLVERUTILS_FORCINGMOVINGBODY
#define NEKTAR_SOLVERUTILS_FORCINGMOVINGBODY

#include <LibUtilities/BasicUtils/NekFactory.hpp>
#include <LibUtilities/BasicUtils/SharedArray.hpp>
#include <LibUtilities/FFT/NektarFFT.h>
#include <SolverUtils/SolverUtilsDeclspec.h>
#include <SolverUtils/Forcing/Forcing.h>
#include <SolverUtils/Filters/FilterAeroForces.h>
#include <IncNavierStokesSolver/Filters/FilterMovingBody.h>
#include <GlobalMapping/Mapping.h>

namespace Nektar
{

class ForcingMovingBody : public SolverUtils::Forcing
{
    public:

        friend class MemoryManager<ForcingMovingBody>;

        /// Creates an instance of this class
        static SolverUtils::ForcingSharedPtr create(
                const LibUtilities::SessionReaderSharedPtr         &pSession,
                const std::weak_ptr<SolverUtils::EquationSystem> &pEquation,
                const Array<OneD, MultiRegions::ExpListSharedPtr>& pFields,
                const unsigned int& pNumForcingFields,
                const TiXmlElement* pForce)
        {
            SolverUtils::ForcingSharedPtr p =
                                    MemoryManager<ForcingMovingBody>::
                                        AllocateSharedPtr(pSession, pEquation);
            p->InitObject(pFields, pNumForcingFields, pForce);
            return p;
        }

        ///Name of the class
        static std::string className;

    protected:
        // Mapping object
        GlobalMapping::MappingSharedPtr               m_mapping;

        virtual void v_InitObject(
            const Array<OneD, MultiRegions::ExpListSharedPtr>& pFields,
            const unsigned int&                         pNumForcingFields,
            const TiXmlElement*                         pForce);

        virtual void v_Apply(
            const Array<OneD, MultiRegions::ExpListSharedPtr>& fields,
            const Array<OneD, Array<OneD, NekDouble> >& inarray,
                  Array<OneD, Array<OneD, NekDouble> >& outarray,
            const NekDouble&                            time);

    private:

        ForcingMovingBody(
                const LibUtilities::SessionReaderSharedPtr         &pSession,
                const std::weak_ptr<SolverUtils::EquationSystem> &pEquation);

        void CheckIsFromFile(const TiXmlElement* pForce);

        void InitialiseCableModel(
            const LibUtilities::SessionReaderSharedPtr& pSession,
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields);

        void InitialiseFilter(
            const LibUtilities::SessionReaderSharedPtr& pSession,
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields,
            const TiXmlElement* pForce);

        void StructureSolver(
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields,
                  int cn, Array<OneD, NekDouble> &HydroForces,
                  Array<OneD, NekDouble> &BodyMotions);

        void EvaluateStructDynModel(
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields,
                  Array<OneD, NekDouble> &Hydroforces,
                  NekDouble time );

        void SetDynEqCoeffMatrix(
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields, int cn);

        void RollOver(Array<OneD, Array<OneD, NekDouble> > &input);

        void MappingBndConditions(
            const Array<OneD, MultiRegions::ExpListSharedPtr> &pFields,
            const Array<OneD, Array<OneD, NekDouble> >        &fields,
                  NekDouble  time);
        SolverUtils::FilterAeroForcesSharedPtr  m_filterForces;
        void ImportFldBase(
        std::string                                  pInfile,
        MultiRegions::ExpListSharedPtr            &locExpList,
        int                                          pSlice, int f);

        int m_movingBodyCalls;     ///< number of times the movbody have been called
        int m_np;                  ///< number of planes per processors
        int m_vdim;                ///< vibration dimension
        int m_intSteps;

        NekDouble m_structrho;     ///< mass of the cable per unit length
        NekDouble m_angular_structrho;
        NekDouble m_structdamp;    ///< damping ratio of the cable
        NekDouble m_angular_structdamp; 
        NekDouble m_structstiff;
        NekDouble m_angular_structstiff;
        NekDouble m_lhom;          ///< length ratio of the cable
        NekDouble m_kinvis;        ///< fluid viscous
        NekDouble m_timestep;      ///< time step
      
        ///
        static NekDouble AdamsBashforth_coeffs[3][3];
        static NekDouble AdamsMoulton_coeffs[3][3];
        LibUtilities::NektarFFTSharedPtr m_FFT;
        ///
        FilterMovingBodySharedPtr m_MovBodyfilter;
        /// storage for the cable's force(x,y) variables and moment
        Array<OneD, NekDouble> m_Aeroforces;
        Array<OneD, Array< OneD, NekDouble> > m_forcing;
        /// storage for the cable's motion(x,y, theta) variables
        Array<OneD, Array<OneD, NekDouble> >m_MotionVars;
        /// fictitious velocity storage
        Array<OneD, Array<OneD, Array<OneD, NekDouble> > > m_fV;
        /// fictitious acceleration storage
        Array<OneD, Array<OneD, Array<OneD, NekDouble> > > m_fA;
        /// matrices in Newmart-beta method
        Array<OneD, DNekMatSharedPtr> m_CoeffMat_A;
        /// matrices in Newmart-beta method
        Array<OneD, DNekMatSharedPtr> m_CoeffMat_B;
        /// [0] is displacements, [1] is velocities, [2] is accelerations
        Array<OneD, std::string> m_funcName;
        /// motion direction: [0] is 'x' and [1] is 'y'
        Array<OneD, std::string> m_motion;
        /// do determine if the the body motion come from an extern file
        Array<OneD, bool>        m_IsFromFile;
        /// Store the derivatives of motion variables in x-direction
        Array<OneD, Array< OneD, NekDouble> > m_zta;
        /// Store the derivatives of motion variables in y-direction
        Array<OneD, Array< OneD, NekDouble> > m_eta;
        /// Store the derivatives of motion variables in theta-direction
        Array<OneD, Array< OneD, NekDouble> > m_delta;
        
        Array<OneD, Array<OneD, NekDouble> >  m_baseflow;
        Array<OneD, Array< OneD, NekDouble> > forces_vel;
        Array<OneD, Array< OneD, NekDouble> > m_force;
        Array<OneD, Array< OneD, NekDouble> > m_force1;
        Array<OneD, Array< OneD, NekDouble> > m_velocity;
        Array<OneD,NekDouble> m_displacement;
        Array<OneD,NekDouble> m_previousDisp;
      
        ///
        unsigned int                    m_outputFrequency;
        Array<OneD, std::ofstream>      m_outputStream;
        std::string                     m_outputFile_fce;
        std::string                     m_outputFile_mot;
};

}

#endif
