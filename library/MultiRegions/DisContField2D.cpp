//////////////////////////////////////////////////////////////////////////////
//
// File DisContField2D.cpp
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
// Description: Field definition for 2D domain with boundary conditions using
// LDG flux.
//
///////////////////////////////////////////////////////////////////////////////

#include <boost/core/ignore_unused.hpp>

#include <MultiRegions/DisContField2D.h>
#include <LocalRegions/MatrixKey.h>
#include <LocalRegions/Expansion2D.h>
#include <LocalRegions/Expansion.h>
#include <LocalRegions/QuadExp.h>
#include <LocalRegions/TriExp.h>
#include <SpatialDomains/MeshGraph.h>
#include <LibUtilities/LinearAlgebra/NekTypeDefs.hpp>
#include <LibUtilities/LinearAlgebra/NekMatrix.hpp>

#include <boost/algorithm/string/predicate.hpp>

using namespace std;

namespace Nektar
{
    namespace MultiRegions
    {
        /**
         * @class DisContField2D
         * Abstraction of a global discontinuous two-dimensional spectral/hp
         * element expansion which approximates the solution of a set of
         * partial differential equations.
         */
        NekDouble vel;
        /**
         * @brief Default constructor.
         */
        DisContField2D::DisContField2D(void)
        : ExpList2D          (),
          m_bndCondExpansions(),
          m_bndCondBndWeight(),
          m_bndConditions    (),
          m_trace            (NullExpListSharedPtr)
        {
        }

        DisContField2D::DisContField2D(
            const DisContField2D &In, 
            const bool            DeclareCoeffPhysArrays)
            : ExpList2D            (In,DeclareCoeffPhysArrays),
              m_bndCondExpansions  (In.m_bndCondExpansions),
              m_bndCondBndWeight   (In.m_bndCondBndWeight),
              m_bndConditions      (In.m_bndConditions),
              m_globalBndMat       (In.m_globalBndMat),
              m_traceMap           (In.m_traceMap),
              m_locTraceToTraceMap (In.m_locTraceToTraceMap),
              m_boundaryEdges      (In.m_boundaryEdges),
              m_periodicVerts      (In.m_periodicVerts),
              m_periodicEdges      (In.m_periodicEdges),
              m_periodicFwdCopy    (In.m_periodicFwdCopy),
              m_periodicBwdCopy    (In.m_periodicBwdCopy),
              m_leftAdjacentEdges  (In.m_leftAdjacentEdges)
        {
            if (In.m_trace)
            {
                m_trace = MemoryManager<ExpList1D>::AllocateSharedPtr(
                    *std::dynamic_pointer_cast<ExpList1D>(In.m_trace),
                    DeclareCoeffPhysArrays);
            }
        }

        /**
         * @brief Constructs a global discontinuous field based on an input
         * mesh with boundary conditions.
         */
        DisContField2D::DisContField2D(
            const LibUtilities::SessionReaderSharedPtr &pSession,
            const SpatialDomains::MeshGraphSharedPtr   &graph2D,
            const std::string                          &variable,
            const bool                                  SetUpJustDG,
            const bool                                  DeclareCoeffPhysArrays,
            const Collections::ImplementationType       ImpType)
            : ExpList2D(pSession, graph2D, DeclareCoeffPhysArrays, variable,
                        ImpType),
              m_bndCondExpansions(),
              m_bndCondBndWeight(),
              m_bndConditions(),
              m_trace(NullExpListSharedPtr),
              m_periodicVerts(),
              m_periodicEdges(),
              m_periodicFwdCopy(),
              m_periodicBwdCopy()
        {

            if (variable.compare("DefaultVar") != 0) // do not set up BCs if default variable
            {
                SpatialDomains::BoundaryConditions bcs(m_session, graph2D);
                GenerateBoundaryConditionExpansion(graph2D, bcs, variable,
                                                   DeclareCoeffPhysArrays);

                if (DeclareCoeffPhysArrays)
                {
                    EvaluateBoundaryConditions(0.0, variable);
                }

                // Find periodic edges for this variable.
                FindPeriodicEdges(bcs, variable);
            }

            if (SetUpJustDG)
            {
                SetUpDG(variable);
                m_locTraceToTraceMap->TraceLocToElmtLocCoeffMap(*this, m_trace);
            }
            else
            {
                // Set element edges to point to Robin BC edges if required.
                int i, cnt;
                Array<OneD, int> ElmtID, EdgeID;
                GetBoundaryToElmtMap(ElmtID, EdgeID);

                for(cnt = i = 0; i < m_bndCondExpansions.size(); ++i)
                {
                    MultiRegions::ExpListSharedPtr locExpList;
                    int e;
                    locExpList = m_bndCondExpansions[i];

                    for(e = 0; e < locExpList->GetExpSize(); ++e)
                    {
                        LocalRegions::Expansion2DSharedPtr exp2d =
                                (*m_exp)[ElmtID[cnt+e]]->
                                        as<LocalRegions::Expansion2D>();
                        LocalRegions::Expansion1DSharedPtr exp1d =
                                locExpList->GetExp(e)->
                                        as<LocalRegions::Expansion1D>();
                        LocalRegions::ExpansionSharedPtr   exp =
                                locExpList->GetExp(e)->
                                        as<LocalRegions::Expansion>  ();

                        exp2d->SetEdgeExp(EdgeID[cnt+e], exp1d);
                        exp1d->SetAdjacentElementExp(EdgeID[cnt+e], exp2d);
                    }
                    cnt += m_bndCondExpansions[i]->GetExpSize();
                }

                if(m_session->DefinesSolverInfo("PROJECTION"))
                {
                    std::string ProjectStr =
                         m_session->GetSolverInfo("PROJECTION");
                    if((ProjectStr == "MixedCGDG") ||
                       (ProjectStr == "Mixed_CG_Discontinuous"))
                    {
                        SetUpDG();
                    }
                    else
                    {
                        SetUpPhysNormals();
                    }
                }
                else
                {
                    SetUpPhysNormals();
                }
            }
        }


        /*
         * @brief Copy type constructor which declares new boundary conditions
         * and re-uses mapping info and trace space if possible
         */
        DisContField2D::DisContField2D(
            const DisContField2D                     &In,
            const SpatialDomains::MeshGraphSharedPtr &graph2D,
            const std::string                        &variable,
            const bool                                SetUpJustDG,
            const bool                                DeclareCoeffPhysArrays):
            ExpList2D(In,DeclareCoeffPhysArrays),
              m_trace(NullExpListSharedPtr)
        {
            // Set up boundary conditions for this variable.
            // Do not set up BCs if default variable
            if(variable.compare("DefaultVar") != 0)
            {
                SpatialDomains::BoundaryConditions bcs(m_session, graph2D);
                GenerateBoundaryConditionExpansion(graph2D, bcs, variable);

                if (DeclareCoeffPhysArrays)
                {
                    EvaluateBoundaryConditions(0.0, variable);
                }

                if (!SameTypeOfBoundaryConditions(In))
                {
                    // Find periodic edges for this variable.
                    FindPeriodicEdges(bcs, variable);

                    if(SetUpJustDG)
                    {
                        SetUpDG();
                        m_locTraceToTraceMap->TraceLocToElmtLocCoeffMap(
                            *this, m_trace);
                    }
                    else
                    {
                        // set elmt edges to point to robin bc edges if required
                        int i, cnt = 0;
                        Array<OneD, int> ElmtID,EdgeID;
                        GetBoundaryToElmtMap(ElmtID,EdgeID);

                        for (i = 0; i < m_bndCondExpansions.size(); ++i)
                        {
                            MultiRegions::ExpListSharedPtr locExpList;

                            int e;
                            locExpList = m_bndCondExpansions[i];

                            for(e = 0; e < locExpList->GetExpSize(); ++e)
                            {
                                LocalRegions::Expansion2DSharedPtr exp2d
                                    = (*m_exp)[ElmtID[cnt+e]]->
                                    as<LocalRegions::Expansion2D>();
                                LocalRegions::Expansion1DSharedPtr exp1d
                                    = locExpList->GetExp(e)->
                                    as<LocalRegions::Expansion1D>();
                                LocalRegions::ExpansionSharedPtr   exp
                                    = locExpList->GetExp(e)->
                                    as<LocalRegions::Expansion>  ();

                                exp2d->SetEdgeExp(EdgeID[cnt+e], exp1d);
                                exp1d->SetAdjacentElementExp(EdgeID[cnt+e],
                                                             exp2d);
                            }

                            cnt += m_bndCondExpansions[i]->GetExpSize();
                        }


                        if (m_session->DefinesSolverInfo("PROJECTION"))
                        {
                            std::string ProjectStr =
                                m_session->GetSolverInfo("PROJECTION");

                            if ((ProjectStr == "MixedCGDG") ||
                                (ProjectStr == "Mixed_CG_Discontinuous"))
                            {
                                SetUpDG();
                            }
                            else
                            {
                                SetUpPhysNormals();
                            }
                        }
                        else
                        {
                            SetUpPhysNormals();
                        }
                    }
                }
                else
                {
                    if (SetUpJustDG)
                    {
                        m_globalBndMat       = In.m_globalBndMat;
                        m_trace              = In.m_trace;
                        m_traceMap           = In.m_traceMap;
                        m_locTraceToTraceMap = In.m_locTraceToTraceMap;
                        m_periodicEdges      = In.m_periodicEdges;
                        m_periodicVerts      = In.m_periodicVerts;
                        m_periodicFwdCopy    = In.m_periodicFwdCopy;
                        m_periodicBwdCopy    = In.m_periodicBwdCopy;
                        m_boundaryEdges      = In.m_boundaryEdges;
                        m_leftAdjacentEdges  = In.m_leftAdjacentEdges;
                    }
                    else
                    {
                        m_globalBndMat       = In.m_globalBndMat;
                        m_trace              = In.m_trace;
                        m_traceMap           = In.m_traceMap;
                        m_locTraceToTraceMap = In.m_locTraceToTraceMap;
                        m_periodicEdges      = In.m_periodicEdges;
                        m_periodicVerts      = In.m_periodicVerts;
                        m_periodicFwdCopy    = In.m_periodicFwdCopy;
                        m_periodicBwdCopy    = In.m_periodicBwdCopy;
                        m_boundaryEdges      = In.m_boundaryEdges;
                        m_leftAdjacentEdges  = In.m_leftAdjacentEdges;

                        // set elmt edges to point to robin bc edges if required
                        int i, cnt = 0;
                        Array<OneD, int> ElmtID, EdgeID;
                        GetBoundaryToElmtMap(ElmtID, EdgeID);

                        for (i = 0; i < m_bndCondExpansions.size(); ++i)
                        {
                            MultiRegions::ExpListSharedPtr locExpList;

                            int e;
                            locExpList = m_bndCondExpansions[i];

                            for (e = 0; e < locExpList->GetExpSize(); ++e)
                            {
                                LocalRegions::Expansion2DSharedPtr exp2d
                                    = (*m_exp)[ElmtID[cnt+e]]->
                                    as<LocalRegions::Expansion2D>();
                                LocalRegions::Expansion1DSharedPtr exp1d
                                    = locExpList->GetExp(e)->
                                    as<LocalRegions::Expansion1D>();
                                LocalRegions::ExpansionSharedPtr   exp
                                    = locExpList->GetExp(e)->
                                    as<LocalRegions::Expansion>  ();

                                exp2d->SetEdgeExp(EdgeID[cnt+e], exp1d);
                                exp1d->SetAdjacentElementExp(EdgeID[cnt+e],
                                                             exp2d);
                            }
                            cnt += m_bndCondExpansions[i]->GetExpSize();
                        }

                        SetUpPhysNormals();
                    }
                }
            }
        }

        /**
         * @brief Default destructor.
         */
        DisContField2D::~DisContField2D()
        {
        }

        GlobalLinSysSharedPtr DisContField2D::GetGlobalBndLinSys(
            const GlobalLinSysKey &mkey)
        {
            ASSERTL0(mkey.GetMatrixType() == StdRegions::eHybridDGHelmBndLam,
                     "Routine currently only tested for HybridDGHelmholtz");
            ASSERTL1(mkey.GetGlobalSysSolnType() ==
                         m_traceMap->GetGlobalSysSolnType(),
                     "The local to global map is not set up for the requested "
                     "solution type");

            GlobalLinSysSharedPtr glo_matrix;
            auto matrixIter = m_globalBndMat->find(mkey);

            if(matrixIter == m_globalBndMat->end())
            {
                glo_matrix = GenGlobalBndLinSys(mkey,m_traceMap);
                (*m_globalBndMat)[mkey] = glo_matrix;
            }
            else
            {
                glo_matrix = matrixIter->second;
            }

            return glo_matrix;
        }

        /**
         * @brief Set up all DG member variables and maps.
         */
        void DisContField2D::SetUpDG(const std::string variable)
        {
            // Check for multiple calls
            if (m_trace != NullExpListSharedPtr)
            {
                return;
            }

            ExpList1DSharedPtr trace;

            // Set up matrix map
            m_globalBndMat = MemoryManager<GlobalLinSysMap>::
                AllocateSharedPtr();

            // Set up trace space
            trace = MemoryManager<ExpList1D>::AllocateSharedPtr(
                m_session, m_bndCondExpansions, m_bndConditions, *m_exp,
                m_graph, m_periodicEdges);

            m_trace = std::dynamic_pointer_cast<ExpList>(trace);
            m_traceMap = MemoryManager<AssemblyMapDG>::
                AllocateSharedPtr(m_session, m_graph, trace, *this,
                                  m_bndCondExpansions, m_bndConditions,
                                  m_periodicEdges,
                                  variable);

            if (m_session->DefinesCmdLineArgument("verbose"))
            {
                m_traceMap->PrintStats(std::cout, variable);
            }

            Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

            // Scatter trace segments to 2D elements. For each element,
            // we find the trace segment associated to each edge. The
            // element then retains a pointer to the trace space segments,
            // to ensure uniqueness of normals when retrieving from two
            //adjoining elements which do not lie in a plane.
            for (int i = 0; i < m_exp->size(); ++i)
            {
                for (int j = 0; j < (*m_exp)[i]->GetNedges(); ++j)
                {
                    LocalRegions::Expansion2DSharedPtr exp2d =
                            (*m_exp)[i]->as<LocalRegions::Expansion2D>();
                    LocalRegions::Expansion1DSharedPtr exp1d =
                            elmtToTrace[i][j]->as<LocalRegions::Expansion1D>();
                    exp2d->SetEdgeExp           (j, exp1d);
                    exp1d->SetAdjacentElementExp(j, exp2d);
                }
            }

            // Set up physical normals
            SetUpPhysNormals();
                
            int cnt, n, e;

            // Identify boundary edges
            for (cnt = 0, n = 0; n < m_bndCondExpansions.size(); ++n)
            {
                if (m_bndConditions[n]->GetBoundaryConditionType() !=
                    SpatialDomains::ePeriodic)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        m_boundaryEdges.insert(
                            m_traceMap->GetBndCondIDToGlobalTraceID(cnt+e));
                    }
                    cnt += m_bndCondExpansions[n]->GetExpSize();
                }
            }

            // Set up information for periodic boundary conditions.
            std::unordered_map<int,pair<int,int> > perEdgeToExpMap;
            for (cnt = n = 0; n < m_exp->size(); ++n)
            {
                for (e = 0; e < (*m_exp)[n]->GetNedges(); ++e, ++cnt)
                {
                    auto it = m_periodicEdges.find(
                        (*m_exp)[n]->GetGeom()->GetEid(e));

                    if (it != m_periodicEdges.end())
                    {
                        perEdgeToExpMap[it->first] = make_pair(n, e);
                    }
                }
            }

            // Set up left-adjacent edge list.
            m_leftAdjacentEdges.resize(cnt);
            cnt = 0;
            for (int i = 0; i < m_exp->size(); ++i)
            {
                for (int j = 0; j < (*m_exp)[i]->GetNedges(); ++j, ++cnt)
                {
                    m_leftAdjacentEdges[cnt] = IsLeftAdjacentEdge(i, j);
                }
            }

            // Set up mapping to copy Fwd of periodic bcs to Bwd of other edge.
            cnt = 0;
            for (int n = 0; n < m_exp->size(); ++n)
            {
                for (int e = 0; e < (*m_exp)[n]->GetNedges(); ++e, ++cnt)
                {
                    int edgeGeomId = (*m_exp)[n]->GetGeom()->GetEid(e);
                    int offset = m_trace->GetPhys_Offset(
                        elmtToTrace[n][e]->GetElmtId());

                    // Check to see if this face is periodic.
                    auto it = m_periodicEdges.find(edgeGeomId);

                    if (it != m_periodicEdges.end())
                    {
                        const PeriodicEntity &ent = it->second[0];
                        auto it2 = perEdgeToExpMap.find(ent.id);

                        if (it2 == perEdgeToExpMap.end())
                        {
                            if (m_session->GetComm()->
                                GetRowComm()->GetSize() > 1 && !ent.isLocal)
                            {
                                continue;
                            }
                            else
                            {
                                ASSERTL1(false, "Periodic edge not found!");
                            }
                        }

                        ASSERTL1(m_leftAdjacentEdges[cnt],
                                 "Periodic edge in non-forward space?");

                        int offset2 = m_trace->GetPhys_Offset(
                            elmtToTrace[it2->second.first][it2->second.second]->
                                GetElmtId());

                        // Calculate relative orientations between edges to
                        // calculate copying map.
                        int nquad = elmtToTrace[n][e]->GetNumPoints(0);

                        vector<int> tmpBwd(nquad);
                        vector<int> tmpFwd(nquad);

                        if (ent.orient == StdRegions::eForwards)
                        {
                            for (int i = 0; i < nquad; ++i)
                            {
                                tmpBwd[i] = offset2 + i;
                                tmpFwd[i] = offset  + i;
                            }
                        }
                        else
                        {
                            for (int i = 0; i < nquad; ++i)
                            {
                                tmpBwd[i] = offset2 + i;
                                tmpFwd[i] = offset  + nquad - i - 1;
                            }
                        }

                        for (int i = 0; i < nquad; ++i)
                        {
                            m_periodicFwdCopy.push_back(tmpFwd[i]);
                            m_periodicBwdCopy.push_back(tmpBwd[i]);
                        }
                    }
                }
            }

            m_locTraceToTraceMap = MemoryManager<LocTraceToTraceMap>::
                AllocateSharedPtr(*this, m_trace, elmtToTrace,
                                  m_leftAdjacentEdges);
        }

        /**
         * For each boundary region, checks that the types and number of
         * boundary expansions in that region match.
         * @param   In          ContField2D to compare with.
         * @return True if boundary conditions match.
         */
        bool DisContField2D::SameTypeOfBoundaryConditions(
            const DisContField2D &In)
        {
            int i;
            bool returnval = true;

            for(i = 0; i < m_bndConditions.size(); ++i)
            {

                // check to see if boundary condition type is the same
                // and there are the same number of boundary
                // conditions in the boundary definition.
                if((m_bndConditions[i]->GetBoundaryConditionType()
                    != In.m_bndConditions[i]->GetBoundaryConditionType())||
                   (m_bndCondExpansions[i]->GetExpSize()
                    != In.m_bndCondExpansions[i]->GetExpSize()))
                {
                    returnval = false;
                    break;
                }
            }

            // Compare with all other processes. Return true only if all
            // processes report having the same boundary conditions.
            int vSame = (returnval?1:0);
            m_comm->GetRowComm()->AllReduce(vSame, LibUtilities::ReduceMin);

            return (vSame == 1);
        }

        /**
         * \brief This function discretises the boundary conditions by setting
         * up a list of one-dimensional boundary expansions.
         *
         * According to their boundary region, the separate segmental boundary
         * expansions are bundled together in an object of the class
         * MultiRegions#ExpList1D.
         *
         * \param graph2D   A mesh, containing information about the domain and
         *                  the spectral/hp element expansion.
         * \param bcs       An entity containing information about the
         *                  boundaries and boundary conditions.
         * \param variable  An optional parameter to indicate for which variable
         *                  the boundary conditions should be discretised.
         */
        void DisContField2D::GenerateBoundaryConditionExpansion(
            const SpatialDomains::MeshGraphSharedPtr &graph2D,
            const SpatialDomains::BoundaryConditions &bcs,
            const std::string &variable,
            const bool DeclareCoeffPhysArrays)
        {
            int cnt = 0;
            SpatialDomains::BoundaryConditionShPtr             bc;
            MultiRegions::ExpList1DSharedPtr                   locExpList;
            const SpatialDomains::BoundaryRegionCollection    &bregions =
                bcs.GetBoundaryRegions();
            const SpatialDomains::BoundaryConditionCollection &bconditions =
                bcs.GetBoundaryConditions();

            m_bndCondExpansions =
                Array<OneD, MultiRegions::ExpListSharedPtr>(bregions.size());
            m_bndConditions     =
                Array<OneD, SpatialDomains::BoundaryConditionShPtr>(bregions.size());

            m_bndCondBndWeight = Array<OneD, NekDouble> {bregions.size(), 0.0};

            for (auto &it : bregions)
            {
                bc = GetBoundaryCondition(bconditions, it.first, variable);

                locExpList = MemoryManager<MultiRegions::ExpList1D>
                    ::AllocateSharedPtr(m_session, *(it.second), graph2D,
                                        DeclareCoeffPhysArrays, variable,
                                        bc->GetComm());

                m_bndCondExpansions[cnt]  = locExpList;
                m_bndConditions[cnt]      = bc;

                std::string type = m_bndConditions[cnt]->GetUserDefined();

                // Set up normals on non-Dirichlet boundary conditions. Second
                // two conditions ideally should be in local solver setup (when
                // made into factory)
                if(bc->GetBoundaryConditionType() != SpatialDomains::eDirichlet
                   || boost::iequals(type,"I") || boost::iequals(type,"CalcBC"))
                {
                    SetUpPhysNormals();
                }

                cnt++;
            }
        }

        /**
         * @brief Determine the periodic edges and vertices for the given graph.
         *
         * Note that much of this routine is the same as the three-dimensional
         * version, which therefore has much better documentation.
         *
         * @param   bcs         Information about the boundary conditions.
         * @param   variable    Specifies the field.
         *
         * @see DisContField3D::FindPeriodicFaces
         */
        void DisContField2D::FindPeriodicEdges(
            const SpatialDomains::BoundaryConditions &bcs,
            const std::string                        &variable)
        {
            const SpatialDomains::BoundaryRegionCollection &bregions
                = bcs.GetBoundaryRegions();
            const SpatialDomains::BoundaryConditionCollection &bconditions
                = bcs.GetBoundaryConditions();

            LibUtilities::CommSharedPtr       vComm       =
                m_session->GetComm()->GetRowComm();
            SpatialDomains::CompositeOrdering compOrder   =
                m_graph->GetCompositeOrdering();
            SpatialDomains::BndRegionOrdering bndRegOrder =
                m_graph->GetBndRegionOrdering();
            SpatialDomains::CompositeMap      compMap     =
                m_graph->GetComposites();

            // Unique collection of pairs of periodic composites (i.e. if
            // composites 1 and 2 are periodic then this map will contain either
            // the pair (1,2) or (2,1) but not both).
            map<int,int>                                 perComps;
            map<int,vector<int>>                         allVerts;
            set<int>                                     locVerts;
            map<int, pair<int, StdRegions::Orientation>> allEdges;

            int region1ID, region2ID, i, j, k, cnt;
            SpatialDomains::BoundaryConditionShPtr locBCond;

            // Set up a set of all local verts and edges.
            for(i = 0; i < (*m_exp).size(); ++i)
            {
                for(j = 0; j < (*m_exp)[i]->GetNverts(); ++j)
                {
                    int id = (*m_exp)[i]->GetGeom()->GetVid(j);
                    locVerts.insert(id);
                }
            }

            // Construct list of all periodic pairs local to this process.
            for (auto &it : bregions)
            {
                locBCond = GetBoundaryCondition(
                    bconditions, it.first, variable);

                if (locBCond->GetBoundaryConditionType()
                        != SpatialDomains::ePeriodic)
                {
                    continue;
                }

                // Identify periodic boundary region IDs.
                region1ID = it.first;
                region2ID = std::static_pointer_cast<
                    SpatialDomains::PeriodicBoundaryCondition>(
                        locBCond)->m_connectedBoundaryRegion;

                // From this identify composites. Note that in serial this will
                // be an empty map.
                int cId1, cId2;
                if (vComm->GetSize() == 1)
                {
                    cId1 = it.second->begin()->first;
                    cId2 = bregions.find(region2ID)->second->begin()->first;
                }
                else
                {
                    cId1 = bndRegOrder.find(region1ID)->second[0];
                    cId2 = bndRegOrder.find(region2ID)->second[0];
                }

                ASSERTL0(it.second->size() == 1,
                         "Boundary region "+boost::lexical_cast<string>(
                             region1ID)+" should only contain 1 composite.");

                // Construct set containing all periodic edges on this process
                SpatialDomains::CompositeSharedPtr c = it.second->begin()->second;

                vector<unsigned int> tmpOrder;

                for (i = 0; i < c->m_geomVec.size(); ++i)
                {
                    SpatialDomains::SegGeomSharedPtr segGeom =
                        std::dynamic_pointer_cast<
                            SpatialDomains::SegGeom>(c->m_geomVec[i]);
                    ASSERTL0(segGeom, "Unable to cast to shared ptr");

                    SpatialDomains::GeometryLinkSharedPtr elmt =
                        m_graph->GetElementsFromEdge(segGeom);
                    ASSERTL0(elmt->size() == 1,
                             "The periodic boundaries belong to "
                             "more than one element of the mesh");

                    SpatialDomains::Geometry2DSharedPtr geom =
                        std::dynamic_pointer_cast<SpatialDomains::Geometry2D>(
                            elmt->at(0).first);

                    allEdges[c->m_geomVec[i]->GetGlobalID()] =
                        make_pair(elmt->at(0).second,
                                  geom->GetEorient(elmt->at(0).second));

                    // In serial mesh partitioning will not have occurred so
                    // need to fill composite ordering map manually.
                    if (vComm->GetSize() == 1)
                    {
                        tmpOrder.push_back(c->m_geomVec[i]->GetGlobalID());
                    }

                    vector<int> vertList(2);
                    vertList[0] = segGeom->GetVid(0);
                    vertList[1] = segGeom->GetVid(1);
                    allVerts[c->m_geomVec[i]->GetGlobalID()] = vertList;
                }

                if (vComm->GetSize() == 1)
                {
                    compOrder[it.second->begin()->first] = tmpOrder;
                }

                // See if we already have either region1 or region2 stored in
                // perComps map.
                if (perComps.count(cId1) == 0)
                {
                    if (perComps.count(cId2) == 0)
                    {
                        perComps[cId1] = cId2;
                    }
                    else
                    {
                        std::stringstream ss;
                        ss << "Boundary region " << cId2 << " should be "
                           << "periodic with " << perComps[cId2] << " but "
                           << "found " << cId1 << " instead!";
                        ASSERTL0(perComps[cId2] == cId1, ss.str());
                    }
                }
                else
                {
                    std::stringstream ss;
                    ss << "Boundary region " << cId1 << " should be "
                       << "periodic with " << perComps[cId1] << " but "
                       << "found " << cId2 << " instead!";
                    ASSERTL0(perComps[cId1] == cId1, ss.str());
                }
            }

            // Process local edge list to obtain relative edge orientations.
            int              n = vComm->GetSize();
            int              p = vComm->GetRank();
            int              totEdges;
            Array<OneD, int> edgecounts(n,0);
            Array<OneD, int> edgeoffset(n,0);
            Array<OneD, int> vertoffset(n,0);

            edgecounts[p] = allEdges.size();
            vComm->AllReduce(edgecounts, LibUtilities::ReduceSum);

            edgeoffset[0] = 0;
            for (i = 1; i < n; ++i)
            {
                edgeoffset[i] = edgeoffset[i-1] + edgecounts[i-1];
            }

            totEdges = Vmath::Vsum(n, edgecounts, 1);
            Array<OneD, int> edgeIds   (totEdges, 0);
            Array<OneD, int> edgeIdx   (totEdges, 0);
            Array<OneD, int> edgeOrient(totEdges, 0);
            Array<OneD, int> edgeVerts (totEdges, 0);

            auto sIt = allEdges.begin();

            for (i = 0; sIt != allEdges.end(); ++sIt)
            {
                edgeIds   [edgeoffset[p] + i  ] = sIt->first;
                edgeIdx   [edgeoffset[p] + i  ] = sIt->second.first;
                edgeOrient[edgeoffset[p] + i  ] = sIt->second.second;
                edgeVerts [edgeoffset[p] + i++] = allVerts[sIt->first].size();
            }

            vComm->AllReduce(edgeIds,    LibUtilities::ReduceSum);
            vComm->AllReduce(edgeIdx,    LibUtilities::ReduceSum);
            vComm->AllReduce(edgeOrient, LibUtilities::ReduceSum);
            vComm->AllReduce(edgeVerts,  LibUtilities::ReduceSum);

            // Calculate number of vertices on each processor.
            Array<OneD, int> procVerts(n,0);
            int nTotVerts;

            // Note if there are no periodic edges at all calling Vsum will
            // cause a segfault.
            if (totEdges > 0)
            {
                nTotVerts = Vmath::Vsum(totEdges, edgeVerts, 1);
            }
            else
            {
                nTotVerts = 0;
            }

            for (i = 0; i < n; ++i)
            {
                if (edgecounts[i] > 0)
                {
                    procVerts[i] = Vmath::Vsum(
                        edgecounts[i], edgeVerts + edgeoffset[i], 1);
                }
                else
                {
                    procVerts[i] = 0;
                }
            }
            vertoffset[0] = 0;

            for (i = 1; i < n; ++i)
            {
                vertoffset[i] = vertoffset[i-1] + procVerts[i-1];
            }

            Array<OneD, int> vertIds(nTotVerts, 0);
            for (i = 0, sIt = allEdges.begin(); sIt != allEdges.end(); ++sIt)
            {
                for (j = 0; j < allVerts[sIt->first].size(); ++j)
                {
                    vertIds[vertoffset[p] + i++] = allVerts[sIt->first][j];
                }
            }

            vComm->AllReduce(vertIds, LibUtilities::ReduceSum);

            // For simplicity's sake create a map of edge id -> orientation.
            map<int, StdRegions::Orientation> orientMap;
            map<int, vector<int> >            vertMap;

            for (cnt = i = 0; i < totEdges; ++i)
            {
                ASSERTL0(orientMap.count(edgeIds[i]) == 0,
                         "Already found edge in orientation map!");

                // Work out relative orientations. To avoid having to exchange
                // vertex locations, we figure out if the edges are backwards or
                // forwards orientated with respect to a forwards orientation
                // that is CCW. Since our local geometries are
                // forwards-orientated with respect to the Cartesian axes, we
                // need to invert the orientation for the top and left edges of
                // a quad and the left edge of a triangle.
                StdRegions::Orientation o =
                    (StdRegions::Orientation)edgeOrient[i];

                if (edgeIdx[i] > 1)
                {
                    o = o == StdRegions::eForwards ?
                        StdRegions::eBackwards : StdRegions::eForwards;
                }

                orientMap[edgeIds[i]] = o;

                vector<int> verts(edgeVerts[i]);

                for (j = 0; j < edgeVerts[i]; ++j)
                {
                    verts[j] = vertIds[cnt++];
                }
                vertMap[edgeIds[i]] = verts;
            }

            // Go through list of composites and figure out which edges are
            // parallel from original ordering in session file. This includes
            // composites which are not necessarily on this process.
            map<int,int> allCompPairs;

            // Store temporary map of periodic vertices which will hold all
            // periodic vertices on the entire mesh so that doubly periodic
            // vertices can be counted properly across partitions. Local
            // vertices are copied into m_periodicVerts at the end of the
            // function.
            PeriodicMap periodicVerts;

            for (auto &cIt : perComps)
            {
                SpatialDomains::CompositeSharedPtr c[2];
                const int   id1  = cIt.first;
                const int   id2  = cIt.second;
                std::string id1s = boost::lexical_cast<string>(id1);
                std::string id2s = boost::lexical_cast<string>(id2);

                if (compMap.count(id1) > 0)
                {
                    c[0] = compMap[id1];
                }

                if (compMap.count(id2) > 0)
                {
                    c[1] = compMap[id2];
                }

                ASSERTL0(c[0] || c[1],
                         "Both composites not found on this process!");

                // Loop over composite ordering to construct list of all
                // periodic edges regardless of whether they are on this
                // process.
                map<int,int> compPairs;

                ASSERTL0(compOrder.count(id1) > 0,
                         "Unable to find composite "+id1s+" in order map.");
                ASSERTL0(compOrder.count(id2) > 0,
                         "Unable to find composite "+id2s+" in order map.");
                ASSERTL0(compOrder[id1].size() == compOrder[id2].size(),
                         "Periodic composites "+id1s+" and "+id2s+
                         " should have the same number of elements.");
                ASSERTL0(compOrder[id1].size() > 0,
                         "Periodic composites "+id1s+" and "+id2s+
                         " are empty!");

                // TODO: Add more checks.
                for (i = 0; i < compOrder[id1].size(); ++i)
                {
                    int eId1 = compOrder[id1][i];
                    int eId2 = compOrder[id2][i];

                    ASSERTL0(compPairs.count(eId1) == 0,
                             "Already paired.");

                    if (compPairs.count(eId2) != 0)
                    {
                        ASSERTL0(compPairs[eId2] == eId1, "Pairing incorrect");
                    }
                    compPairs[eId1] = eId2;
                }

                // Construct set of all edges that we have locally on this
                // processor.
                set<int> locEdges;
                for (i = 0; i < 2; ++i)
                {
                    if (!c[i])
                    {
                        continue;
                    }

                    if (c[i]->m_geomVec.size() > 0)
                    {
                        for (j = 0; j < c[i]->m_geomVec.size(); ++j)
                        {
                            locEdges.insert(c[i]->m_geomVec[j]->GetGlobalID());
                        }
                    }
                }

                // Loop over all edges in the geometry composite.
                for (auto &pIt : compPairs)
                {
                    int  ids  [2] = {pIt.first, pIt.second};
                    bool local[2] = {locEdges.count(pIt.first) > 0,
                                     locEdges.count(pIt.second) > 0};

                    ASSERTL0(orientMap.count(ids[0]) > 0 &&
                             orientMap.count(ids[1]) > 0,
                             "Unable to find edge in orientation map.");

                    allCompPairs[pIt.first ] = pIt.second;
                    allCompPairs[pIt.second] = pIt.first;

                    for (i = 0; i < 2; ++i)
                    {
                        if (!local[i])
                        {
                            continue;
                        }

                        int other = (i+1) % 2;

                        StdRegions::Orientation o =
                            orientMap[ids[i]] == orientMap[ids[other]] ?
                                StdRegions::eBackwards :
                                StdRegions::eForwards;

                        PeriodicEntity ent(ids  [other], o,
                                           local[other]);
                        m_periodicEdges[ids[i]].push_back(ent);
                    }

                    for (i = 0; i < 2; ++i)
                    {
                        int other = (i+1) % 2;

                        StdRegions::Orientation o =
                            orientMap[ids[i]] == orientMap[ids[other]] ?
                                StdRegions::eBackwards :
                            StdRegions::eForwards;

                        // Determine periodic vertices.
                        vector<int> perVerts1 = vertMap[ids[i]];
                        vector<int> perVerts2 = vertMap[ids[other]];

                        map<int, pair<int, bool> > tmpMap;
                        if (o == StdRegions::eForwards)
                        {
                            tmpMap[perVerts1[0]] = make_pair(
                                perVerts2[0], locVerts.count(perVerts2[0]) > 0);
                            tmpMap[perVerts1[1]] = make_pair(
                                perVerts2[1], locVerts.count(perVerts2[1]) > 0);
                        }
                        else
                        {
                            tmpMap[perVerts1[0]] = make_pair(
                                perVerts2[1], locVerts.count(perVerts2[1]) > 0);
                            tmpMap[perVerts1[1]] = make_pair(
                                perVerts2[0], locVerts.count(perVerts2[0]) > 0);
                        }

                        for (auto &mIt : tmpMap)
                        {
                            // See if this vertex has been recorded already.
                            PeriodicEntity ent2(mIt.second.first,
                                                StdRegions::eNoOrientation,
                                                mIt.second.second);
                            auto perIt = periodicVerts.find(mIt.first);

                            if (perIt == periodicVerts.end())
                            {
                                periodicVerts[mIt.first].push_back(ent2);
                                perIt = periodicVerts.find(mIt.first);
                            }
                            else
                            {
                                bool doAdd = true;
                                for (j = 0; j < perIt->second.size(); ++j)
                                {
                                    if (perIt->second[j].id == mIt.second.first)
                                    {
                                        doAdd = false;
                                        break;
                                    }
                                }

                                if (doAdd)
                                {
                                    perIt->second.push_back(ent2);
                                }
                            }
                        }
                    }
                }
            }

            Array<OneD, int> pairSizes(n, 0);
            pairSizes[p] = allCompPairs.size();
            vComm->AllReduce(pairSizes, LibUtilities::ReduceSum);

            int totPairSizes = Vmath::Vsum(n, pairSizes, 1);

            Array<OneD, int> pairOffsets(n, 0);
            pairOffsets[0] = 0;

            for (i = 1; i < n; ++i)
            {
                pairOffsets[i] = pairOffsets[i-1] + pairSizes[i-1];
            }

            Array<OneD, int> first (totPairSizes, 0);
            Array<OneD, int> second(totPairSizes, 0);

            cnt = pairOffsets[p];

            for (auto &pIt : allCompPairs)
            {
                first [cnt  ] = pIt.first;
                second[cnt++] = pIt.second;
            }

            vComm->AllReduce(first,  LibUtilities::ReduceSum);
            vComm->AllReduce(second, LibUtilities::ReduceSum);

            allCompPairs.clear();

            for(cnt = 0; cnt < totPairSizes; ++cnt)
            {
                allCompPairs[first[cnt]] = second[cnt];
            }

            // Search for periodic vertices and edges which are not in
            // a periodic composite but lie in this process. First, loop
            // over all information we have from other processors.
            for (cnt = i = 0; i < totEdges; ++i)
            {
                int edgeId    = edgeIds[i];

                ASSERTL0(allCompPairs.count(edgeId) > 0,
                         "Unable to find matching periodic edge.");

                int perEdgeId = allCompPairs[edgeId];

                for (j = 0; j < edgeVerts[i]; ++j, ++cnt)
                {
                    int vId = vertIds[cnt];

                    auto perId = periodicVerts.find(vId);

                    if (perId == periodicVerts.end())
                    {
                        // This vertex is not included in the map. Figure
                        // out which vertex it is supposed to be periodic
                        // with. perEdgeId is the edge ID which is periodic with
                        // edgeId. The logic is much the same as the loop above.
                        int perVertexId =
                            orientMap[edgeId] == orientMap[perEdgeId] ?
                            vertMap[perEdgeId][(j+1)%2] : vertMap[perEdgeId][j];

                        PeriodicEntity ent(perVertexId,
                                           StdRegions::eNoOrientation,
                                           locVerts.count(perVertexId) > 0);

                        periodicVerts[vId].push_back(ent);
                    }
                }
            }

            // Loop over all periodic vertices to complete connectivity
            // information.
            for (auto &perIt : periodicVerts)
            {
                // Loop over associated vertices.
                for (i = 0; i < perIt.second.size(); ++i)
                {
                    auto perIt2 = periodicVerts.find(perIt.second[i].id);
                    ASSERTL0(perIt2 != periodicVerts.end(),
                             "Couldn't find periodic vertex.");

                    for (j = 0; j < perIt2->second.size(); ++j)
                    {
                        if (perIt2->second[j].id == perIt.first)
                        {
                            continue;
                        }

                        bool doAdd = true;
                        for (k = 0; k < perIt.second.size(); ++k)
                        {
                            if (perIt2->second[j].id == perIt.second[k].id)
                            {
                                doAdd = false;
                                break;
                            }
                        }

                        if (doAdd)
                        {
                            perIt.second.push_back(perIt2->second[j]);
                        }
                    }
                }
            }

            // Do one final loop over periodic vertices to remove non-local
            // vertices from map.
            for (auto &perIt : periodicVerts)
            {
                if (locVerts.count(perIt.first) > 0)
                {
                    m_periodicVerts.insert(perIt);
                }
            }
        }

        bool DisContField2D::IsLeftAdjacentEdge(const int n, const int e)
        {
            LocalRegions::Expansion1DSharedPtr traceEl =
                    m_traceMap->GetElmtToTrace()[n][e]->
                            as<LocalRegions::Expansion1D>();


            bool fwd = true;
            if (traceEl->GetLeftAdjacentElementEdge () == -1 ||
                traceEl->GetRightAdjacentElementEdge() == -1)
            {
                // Boundary edge (1 connected element). Do nothing in
                // serial.
                auto it = m_boundaryEdges.find(traceEl->GetElmtId());

                // If the edge does not have a boundary condition set on
                // it, then assume it is a partition edge.
                if (it == m_boundaryEdges.end())
                {
                    fwd = true; // Partition edge is always fwd
                }
            }
            else if ( traceEl->GetLeftAdjacentElementEdge () != -1 &&
                      traceEl->GetRightAdjacentElementEdge() != -1 )
            {
                // Non-boundary edge (2 connected elements).
                fwd = ( traceEl->GetLeftAdjacentElementExp().get() == (*m_exp)[n].get() );
            }
            else
            {
                ASSERTL2( false, "Unconnected trace element!" );
            }

            return fwd;
        }
            
        /**
         * @brief This method extracts the "forward" and "backward" trace data
         * from the array @a field and puts the data into output vectors @a Fwd
         * and @a Bwd.
         *
         * We first define the convention which defines "forwards" and
         * "backwards". First an association is made between the edge of each
         * element and its corresponding edge in the trace space using the
         * mapping #m_traceMap. The element can either be left-adjacent or
         * right-adjacent to this trace edge (see
         * Expansion1D::GetLeftAdjacentElementExp). Boundary edges are always
         * left-adjacent since left-adjacency is populated first.
         *
         * If the element is left-adjacent we extract the edge trace data from
         * @a field into the forward trace space @a Fwd; otherwise, we place it
         * in the backwards trace space @a Bwd. In this way, we form a unique
         * set of trace normals since these are always extracted from
         * left-adjacent elements.
         *
         * @param field is a NekDouble array which contains the 2D data
         *              from which we wish to extract the backward and
         *              forward orientated trace/edge arrays.
         * @param Fwd   The resulting forwards space.
         * @param Bwd   The resulting backwards space.
         */
        void DisContField2D::v_GetFwdBwdTracePhysInterior(
            const Array<OneD, const NekDouble> &field,
                  Array<OneD,       NekDouble> &Fwd,
                  Array<OneD,       NekDouble> &Bwd)
        {

            // Zero forward/backward vectors.
            Vmath::Zero(Fwd.size(), Fwd, 1);
            Vmath::Zero(Bwd.size(), Bwd, 1);

            // Basis definition on each element
            LibUtilities::BasisSharedPtr basis = (*m_exp)[0]->GetBasis(0);
            if (basis->GetBasisType() != LibUtilities::eGauss_Lagrange)
            {

                // blocked routine
                Array<OneD, NekDouble> edgevals(m_locTraceToTraceMap->
                                               GetNLocTracePts());

                m_locTraceToTraceMap->LocTracesFromField(field, edgevals);
                m_locTraceToTraceMap->InterpLocEdgesToTrace(0, edgevals, Fwd);

                Array<OneD, NekDouble> invals = edgevals + m_locTraceToTraceMap->
                                                        GetNFwdLocTracePts();
                m_locTraceToTraceMap->InterpLocEdgesToTrace(1, invals, Bwd);
            }
            else
            {
                // Loop over elements and collect forward expansion
                int nexp = GetExpSize();
                Array<OneD,NekDouble> e_tmp;
                LocalRegions::Expansion2DSharedPtr exp2d;

                Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

                int cnt;
                for(int n = cnt = 0; n < nexp; ++n)
                {
                    exp2d = (*m_exp)[n]->as<LocalRegions::Expansion2D>();
                    int phys_offset = GetPhys_Offset(n);

                    for(int e = 0; e < exp2d->GetNedges(); ++e, ++cnt)
                    {
                        int offset = m_trace->GetPhys_Offset(
                            elmtToTrace[n][e]->GetElmtId());

                        if (m_leftAdjacentEdges[cnt])
                        {
                            exp2d->GetEdgePhysVals(e, elmtToTrace[n][e],
                                                   field + phys_offset,
                                                   e_tmp = Fwd + offset);
                        }
                        else
                        {
                            exp2d->GetEdgePhysVals(e, elmtToTrace[n][e],
                                                   field + phys_offset,
                                                   e_tmp = Bwd + offset);
                        }
                    }
                }
            }
            DisContField2D::v_PeriodicBwdCopy(Fwd, Bwd);
        }

        void DisContField2D::v_AddTraceQuadPhysToField(
            const Array<OneD, const NekDouble> &Fwd,
            const Array<OneD, const NekDouble> &Bwd,
                  Array<OneD,       NekDouble> &field)
        {
            // Basis definition on each element
            LibUtilities::BasisSharedPtr basis = (*m_exp)[0]->GetBasis(0);
            if (basis->GetBasisType() != LibUtilities::eGauss_Lagrange)
            {
                Array<OneD, NekDouble> edgevals(m_locTraceToTraceMap->
                                               GetNLocTracePts(), 0.0);

                Array<OneD, NekDouble> invals = edgevals + 
                    m_locTraceToTraceMap->GetNFwdLocTracePts();
                m_locTraceToTraceMap->RightIPTWLocEdgesToTraceInterpMat(
                                        1, Bwd, invals);
                
                m_locTraceToTraceMap->RightIPTWLocEdgesToTraceInterpMat(
                                        0, Fwd, edgevals);

                m_locTraceToTraceMap->AddLocTracesToField(edgevals, field);
            }
            else
            {
                ASSERTL0(false, 
                    "v_AddTraceQuadPhysToField not coded for eGauss_Lagrange");                
            }
        }

        /**
         * @brief Fill the Bwd based on corresponding boundary conditions.
         * Periodic boundary is considered interior traces and 
         * is not treated here.
         */
        void DisContField2D::v_FillBwdWithBound(
            const Array<OneD, const NekDouble> &Fwd,
                  Array<OneD,       NekDouble> &Bwd)
        {
            int cnt = 0;
            int npts = 0;
            int e = 0;

            // Fill boundary conditions into missing elements
            int id1 = 0;
            int id2 = 0;

            for (int n = cnt = 0; n < m_bndCondExpansions.size(); ++n)
            {
                if (m_bndConditions[n]->GetBoundaryConditionType() ==
                        SpatialDomains::eDirichlet)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->GetExp(e)->GetNumPoints(0);
                        id1 = m_bndCondExpansions[n]->GetPhys_Offset(e);
                        id2 = m_trace->GetPhys_Offset(m_traceMap->
                                        GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Vcopy(npts,
                            &(m_bndCondExpansions[n]->GetPhys())[id1], 1,
                            &Bwd[id2],                                 1);
                    }

                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() ==
                             SpatialDomains::eNeumann ||
                         m_bndConditions[n]->GetBoundaryConditionType() ==
                             SpatialDomains::eRobin)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->GetExp(e)->GetNumPoints(0);
                        id1  = m_bndCondExpansions[n]->GetPhys_Offset(e);
                        ASSERTL0((m_bndCondExpansions[n]->GetPhys())[id1]==0.0,
                                 "Method not set up for non-zero Neumann "
                                 "boundary condition");
                        id2  = m_trace->GetPhys_Offset(
                            m_traceMap->GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Vcopy(npts, &Fwd[id2], 1, &Bwd[id2], 1);
                    }

                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() !=
                             SpatialDomains::ePeriodic)
                {
                    ASSERTL0(false,
                             "Method not set up for this boundary condition.");
                }
            }            
        }

        
        /**
         * @brief Fill the Bwd based on corresponding boundary conditions 
         * for derivatives. Periodic boundary is considered interior traces and 
         * is not treated here.
         */
        void DisContField2D::v_FillBwdWithBoundDeriv(
            const int                          Dir,
            const Array<OneD, const NekDouble> &Fwd,
                  Array<OneD,       NekDouble> &Bwd)
        {
            boost::ignore_unused(Dir);
            int cnt;
            int npts;
            int e = 0;

            // Fill boundary conditions into missing elements
            for (int n = cnt = 0; n < m_bndCondExpansions.size(); ++n)
            {
                if (m_bndConditions[n]->GetBoundaryConditionType() == 
                        SpatialDomains::eDirichlet)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->
                                GetExp(e)->GetNumPoints(0);
                        int id2 = m_trace->GetPhys_Offset(m_traceMap->
                                        GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Vcopy(npts, &Fwd[id2], 1, &Bwd[id2], 1);
                    }
                    
                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() == 
                             SpatialDomains::eNeumann || 
                         m_bndConditions[n]->GetBoundaryConditionType() == 
                             SpatialDomains::eRobin)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->
                                GetExp(e)->GetNumPoints(0);
                        int id2 = m_trace->GetPhys_Offset(m_traceMap->
                                        GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Vcopy(npts, &Fwd[id2], 1, &Bwd[id2], 1);
                    }
                    
                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() !=
                             SpatialDomains::ePeriodic)
                {
                    ASSERTL0(false,
                             "Method not set up for this boundary condition.");
                }
            }       
        }

        /**
         * @brief Fill the weight with m_bndCondBndWeight.
         */
        void DisContField2D::v_FillBwdWithBwdWeight(
                  Array<OneD,       NekDouble> &weightave,
                  Array<OneD,       NekDouble> &weightjmp)
        {
            int cnt;
            int npts;
            int e = 0;

            // Fill boundary conditions into missing elements
            int id2 = 0;
            
            for (int n = cnt = 0; n < m_bndCondExpansions.size(); ++n)
            {

                if (m_bndConditions[n]->GetBoundaryConditionType() == 
                        SpatialDomains::eDirichlet)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->
                                GetExp(e)->GetNumPoints(0);
                        id2 = m_trace->GetPhys_Offset(m_traceMap->
                                        GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Fill(npts,m_bndCondBndWeight[n], 
                                    &weightave[id2], 1);
                        Vmath::Fill(npts, 0.0, &weightjmp[id2], 1);

                    }
                    
                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() == 
                             SpatialDomains::eNeumann || 
                         m_bndConditions[n]->GetBoundaryConditionType() == 
                             SpatialDomains::eRobin)
                {
                    for (e = 0; e < m_bndCondExpansions[n]->GetExpSize(); ++e)
                    {
                        npts = m_bndCondExpansions[n]->
                                GetExp(e)->GetNumPoints(0);
                        id2 = m_trace->GetPhys_Offset(m_traceMap->
                                        GetBndCondIDToGlobalTraceID(cnt+e));
                        Vmath::Fill(npts,
                                    m_bndCondBndWeight[n], 
                                    &weightave[id2], 1);
                        Vmath::Fill(npts, 0.0, &weightjmp[id2], 1);
                    }
                    
                    cnt += e;
                }
                else if (m_bndConditions[n]->GetBoundaryConditionType() !=
                             SpatialDomains::ePeriodic)
                {
                    ASSERTL0(false,
                             "Method not set up for this boundary condition.");
                }
            }       
        }
        
        
        void DisContField2D::v_ExtractTracePhys(
            Array<OneD, NekDouble> &outarray)
        {
            ASSERTL1(m_physState == true,
                     "Field must be in physical state to extract trace space.");

            v_ExtractTracePhys(m_phys, outarray);
        }

        /**
         * @brief This method extracts the trace (edges in 2D) from the field @a
         * inarray and puts the values in @a outarray.
         *
         * It assumes the field is C0 continuous so that it can overwrite the
         * edge data when visited by the two adjacent elements.
         *
         * @param inarray   An array containing the 2D data from which we wish
         *                  to extract the edge data.
         * @param outarray  The resulting edge information.
         */
        void DisContField2D::v_ExtractTracePhys(
            const Array<OneD, const NekDouble> &inarray,
                  Array<OneD,       NekDouble> &outarray)
        {
            LibUtilities::BasisSharedPtr basis = (*m_exp)[0]->GetBasis(0);
            if (basis->GetBasisType() != LibUtilities::eGauss_Lagrange)
            {
                Vmath::Zero(outarray.size(), outarray, 1);

                Array<OneD, NekDouble> tracevals(
                                    m_locTraceToTraceMap->GetNFwdLocTracePts());
                m_locTraceToTraceMap->FwdLocTracesFromField(inarray,tracevals);
                m_locTraceToTraceMap->
                            InterpLocEdgesToTrace(0,tracevals,outarray);
                m_traceMap->GetAssemblyCommDG()->PerformExchange(outarray, outarray);
            }
            else
            {
                // Loop over elemente and collect forward expansion
                int nexp = GetExpSize();
                int n, e, offset, phys_offset;
                Array<OneD,NekDouble> e_tmp;
                Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                    &elmtToTrace = m_traceMap->GetElmtToTrace();

                ASSERTL1(outarray.size() >= m_trace->GetNpoints(),
                         "input array is of insufficient length");

                // use m_trace tmp space in element to fill values
                for(n  = 0; n < nexp; ++n)
                {
                    phys_offset = GetPhys_Offset(n);

                    for(e = 0; e < (*m_exp)[n]->GetNedges(); ++e)
                    {
                        offset = m_trace->GetPhys_Offset(
                            elmtToTrace[n][e]->GetElmtId());
                        (*m_exp)[n]->GetEdgePhysVals(e,  elmtToTrace[n][e],
                                                     inarray + phys_offset,
                                                     e_tmp = outarray + offset);
                    }
                }
            }
        }

        void DisContField2D::v_AddTraceIntegral(
            const Array<OneD, const NekDouble> &Fx,
            const Array<OneD, const NekDouble> &Fy,
                  Array<OneD,       NekDouble> &outarray)
        {
            int e, n, offset, t_offset;
            Array<OneD, NekDouble> e_outarray;
            Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

            for(n = 0; n < GetExpSize(); ++n)
            {
                offset = GetCoeff_Offset(n);
                for(e = 0; e < (*m_exp)[n]->GetNedges(); ++e)
                {
                    t_offset = GetTrace()->GetPhys_Offset(
                        elmtToTrace[n][e]->GetElmtId());

                    (*m_exp)[n]->AddEdgeNormBoundaryInt(
                        e,elmtToTrace[n][e],
                        Fx + t_offset,
                        Fy + t_offset,
                        e_outarray = outarray+offset);
                }
            }
        }

        /**
         * @brief Add trace contributions into elemental coefficient spaces.
         *
         * Given some quantity \f$ \vec{Fn} \f$, which conatins this
         * routine calculates the integral
         *
         * \f[
         * \int_{\Omega^e} \vec{Fn}, \mathrm{d}S
         * \f]
         *
         * and adds this to the coefficient space provided by outarray.
         *
         * @see Expansion2D::AddEdgeNormBoundaryInt
         *
         * @param Fn        The trace quantities.
         * @param outarray  Resulting 2D coefficient space.
         */
        void DisContField2D::v_AddTraceIntegral(
            const Array<OneD, const NekDouble> &Fn,
                  Array<OneD,       NekDouble> &outarray)
        {
            // Basis definition on each element
            LibUtilities::BasisSharedPtr basis = (*m_exp)[0]->GetBasis(0);
            if (basis->GetBasisType() != LibUtilities::eGauss_Lagrange)
            {
                Array<OneD, NekDouble> Fcoeffs(m_trace->GetNcoeffs());
                m_trace->IProductWRTBase(Fn, Fcoeffs);

                m_locTraceToTraceMap->AddTraceCoeffsToFieldCoeffs(Fcoeffs,
                                                        outarray);
            }
            else
            {
                int e, n, offset, t_offset;
                Array<OneD, NekDouble> e_outarray;
                Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                    &elmtToTrace = m_traceMap->GetElmtToTrace();

                for(n = 0; n < GetExpSize(); ++n)
                {
                    offset = GetCoeff_Offset(n);
                    for(e = 0; e < (*m_exp)[n]->GetNedges(); ++e)
                    {
                        t_offset = GetTrace()->GetPhys_Offset(
                            elmtToTrace[n][e]->GetElmtId());
                        (*m_exp)[n]->AddEdgeNormBoundaryInt(
                            e, elmtToTrace[n][e],
                            Fn+t_offset,
                            e_outarray = outarray+offset);
                    }
                }
            }
        }


        /**
         * @brief Add trace contributions into elemental coefficient spaces.
         *
         * Given some quantity \f$ \vec{q} \f$, calculate the elemental integral
         *
         * \f[
         * \int_{\Omega^e} \vec{q}, \mathrm{d}S
         * \f]
         *
         * and adds this to the coefficient space provided by
         * outarray. The value of q is determined from the routine
         * IsLeftAdjacentEdge() which if true we use Fwd else we use
         * Bwd
         *
         * @see Expansion2D::AddEdgeNormBoundaryInt
         *
         * @param Fwd       The trace quantities associated with left (fwd)
         *                  adjancent elmt.
         * @param Bwd       The trace quantities associated with right (bwd)
         *                  adjacent elet.
         * @param outarray  Resulting 2D coefficient space.
         */
        void DisContField2D::v_AddFwdBwdTraceIntegral(
            const Array<OneD, const NekDouble> &Fwd,
            const Array<OneD, const NekDouble> &Bwd,
                  Array<OneD,       NekDouble> &outarray)
        {
            int e,n,offset, t_offset;
            Array<OneD, NekDouble> e_outarray;
            Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

            for (n = 0; n < GetExpSize(); ++n)
            {
                offset = GetCoeff_Offset(n);
                for (e = 0; e < (*m_exp)[n]->GetNedges(); ++e)
                {
                    t_offset = GetTrace()->GetPhys_Offset(
                                            elmtToTrace[n][e]->GetElmtId());

                    // Evaluate upwind flux less local edge
                    if (IsLeftAdjacentEdge(n, e))
                    {
                        (*m_exp)[n]->AddEdgeNormBoundaryInt(
                        e, elmtToTrace[n][e], Fwd+t_offset,
                        e_outarray = outarray+offset);
                    }
                    else
                    {
                        (*m_exp)[n]->AddEdgeNormBoundaryInt(
                        e, elmtToTrace[n][e], Bwd+t_offset,
                        e_outarray = outarray+offset);
                    }

                }
            }
        }

        /**
         * @brief Set up a list of element IDs and edge IDs that link to the
         * boundary conditions.
         */
        void DisContField2D::v_GetBoundaryToElmtMap(
            Array<OneD, int> &ElmtID,
            Array<OneD, int> &EdgeID)
        {
            if (m_BCtoElmMap.size() == 0)
            {
                map<int, int> globalIdMap;
                int i,n;
                int cnt;
                int nbcs = 0;

                // Populate global ID map (takes global geometry ID to local
                // expansion list ID).
                for (i = 0; i < GetExpSize(); ++i)
                {
                    globalIdMap[(*m_exp)[i]->GetGeom()->GetGlobalID()] = i;
                }

                // Determine number of boundary condition expansions.
                for(i = 0; i < m_bndConditions.size(); ++i)
                {
                    nbcs += m_bndCondExpansions[i]->GetExpSize();
                }

                // Initialize arrays
                m_BCtoElmMap = Array<OneD, int>(nbcs);
                m_BCtoEdgMap = Array<OneD, int>(nbcs);

                LocalRegions::Expansion1DSharedPtr exp1d;
                for (cnt = n = 0; n < m_bndCondExpansions.size(); ++n)
                {
                    for (i = 0; i < m_bndCondExpansions[n]->GetExpSize();
                         ++i, ++cnt)
                    {
                        exp1d = m_bndCondExpansions[n]->GetExp(i)->
                                            as<LocalRegions::Expansion1D>();
                        // Use edge to element map from MeshGraph.
                        SpatialDomains::GeometryLinkSharedPtr tmp =
                            m_graph->GetElementsFromEdge(exp1d->GetGeom1D());

                        m_BCtoElmMap[cnt] = globalIdMap[
                            (*tmp)[0].first->GetGlobalID()];
                        m_BCtoEdgMap[cnt] = (*tmp)[0].second;
                    }
                }
            }
            ElmtID = m_BCtoElmMap;
            EdgeID = m_BCtoEdgMap;
        }

        void DisContField2D::v_GetBndElmtExpansion(int i,
                            std::shared_ptr<ExpList> &result,
                            const bool DeclareCoeffPhysArrays)
        {
            int n, cnt, nq;
            int offsetOld, offsetNew;
            std::vector<unsigned int> eIDs;

            Array<OneD, int> ElmtID,EdgeID;
            GetBoundaryToElmtMap(ElmtID,EdgeID);

            // Skip other boundary regions
            for (cnt = n = 0; n < i; ++n)
            {
                cnt += m_bndCondExpansions[n]->GetExpSize();
            }

            // Populate eIDs with information from BoundaryToElmtMap
            for (n = 0; n < m_bndCondExpansions[i]->GetExpSize(); ++n)
            {
                eIDs.push_back(ElmtID[cnt+n]);
            }

            // Create expansion list
            result =
                MemoryManager<ExpList2D>::AllocateSharedPtr
                    (*this, eIDs, DeclareCoeffPhysArrays);

            // Copy phys and coeffs to new explist
            if( DeclareCoeffPhysArrays)
            {
                Array<OneD, NekDouble> tmp1, tmp2;
                for (n = 0; n < result->GetExpSize(); ++n)
                {
                    nq = GetExp(ElmtID[cnt+n])->GetTotPoints();
                    offsetOld = GetPhys_Offset(ElmtID[cnt+n]);
                    offsetNew = result->GetPhys_Offset(n);
                    Vmath::Vcopy(nq, tmp1 = GetPhys()+ offsetOld, 1,
                                tmp2 = result->UpdatePhys()+ offsetNew, 1);

                    nq = GetExp(ElmtID[cnt+n])->GetNcoeffs();
                    offsetOld = GetCoeff_Offset(ElmtID[cnt+n]);
                    offsetNew = result->GetCoeff_Offset(n);
                    Vmath::Vcopy(nq, tmp1 = GetCoeffs()+ offsetOld, 1,
                                tmp2 = result->UpdateCoeffs()+ offsetNew, 1);
                }
            }
        }

        /**
         * @brief Reset this field, so that geometry information can be updated.
         */
        void DisContField2D::v_Reset()
        {
            ExpList::v_Reset();

            // Reset boundary condition expansions.
            for (int n = 0; n < m_bndCondExpansions.size(); ++n)
            {
                m_bndCondExpansions[n]->Reset();
                m_bndCondBndWeight[n] = 0.0;
            }
        }

        /**
         * @brief Calculate the \f$ L^2 \f$ error of the \f$ Q_{\rm dir} \f$
         * derivative using the consistent DG evaluation of \f$ Q_{\rm dir} \f$.
         *
         * The solution provided is of the primative variation at the quadrature
         * points and the derivative is compared to the discrete derivative at
         * these points, which is likely to be undesirable unless using a much
         * higher number of quadrature points than the polynomial order used to
         * evaluate \f$ Q_{\rm dir} \f$.
        */
        NekDouble DisContField2D::L2_DGDeriv(
            const int                           dir,
            const Array<OneD, const NekDouble> &soln)
        {
            int    i,e,ncoeff_edge;
            Array<OneD, const NekDouble> tmp_coeffs;
            Array<OneD, NekDouble> out_d(m_ncoeffs), out_tmp;

            Array<OneD, Array<OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

            StdRegions::Orientation edgedir;

            int     cnt;
            int     LocBndCoeffs = m_traceMap->GetNumLocalBndCoeffs();
            Array<OneD, NekDouble> loc_lambda(LocBndCoeffs), edge_lambda;

            
            m_traceMap->GlobalToLocalBnd(m_trace->GetCoeffs(),loc_lambda);

            edge_lambda = loc_lambda;

            // Calculate Q using standard DG formulation.
            for(i = cnt = 0; i < GetExpSize(); ++i)
            {
                // Probably a better way of setting up lambda than this.
                // Note cannot use PutCoeffsInToElmts since lambda space
                // is mapped during the solve.
                int nEdges = (*m_exp)[i]->GetNedges();
                Array<OneD, Array<OneD, NekDouble> > edgeCoeffs(nEdges);

                for(e = 0; e < nEdges; ++e)
                {
                    edgedir = (*m_exp)[i]->GetEorient(e);
                    ncoeff_edge = elmtToTrace[i][e]->GetNcoeffs();
                    edgeCoeffs[e] = Array<OneD, NekDouble>(ncoeff_edge);
                    Vmath::Vcopy(ncoeff_edge, edge_lambda, 1, edgeCoeffs[e], 1);
                    elmtToTrace[i][e]->SetCoeffsToOrientation(
                        edgedir, edgeCoeffs[e], edgeCoeffs[e]);
                    edge_lambda = edge_lambda + ncoeff_edge;
                }

                (*m_exp)[i]->DGDeriv(dir,
                                       tmp_coeffs=m_coeffs+m_coeff_offset[i],
                                       elmtToTrace[i],
                                       edgeCoeffs,
                                       out_tmp = out_d+cnt);
                cnt  += (*m_exp)[i]->GetNcoeffs();
            }

            BwdTrans(out_d,m_phys);
            Vmath::Vsub(m_npoints,m_phys,1,soln,1,m_phys,1);
            return L2(m_phys);
        }

        void DisContField2D::v_HelmSolve(
                const Array<OneD, const NekDouble> &inarray,
                      Array<OneD,       NekDouble> &outarray,
                const StdRegions::ConstFactorMap &factors,
                const StdRegions::VarCoeffMap &varcoeff,
                const MultiRegions::VarFactorsMap &varfactors,
                const Array<OneD, const NekDouble> &dirForcing,
                const bool  PhysSpaceForcing)

        {
            boost::ignore_unused(varfactors, dirForcing);
            int i,j,n,cnt,cnt1,nbndry;
            int nexp = GetExpSize();

            Array<OneD,NekDouble> f(m_ncoeffs);
            DNekVec F(m_ncoeffs,f,eWrapper);
            Array<OneD,NekDouble> e_f, e_l;

            //----------------------------------
            //  Setup RHS Inner product
            //----------------------------------
            if(PhysSpaceForcing)
            {
                IProductWRTBase(inarray,f);
                Vmath::Neg(m_ncoeffs,f,1);
            }
            else
            {
                Vmath::Smul(m_ncoeffs,-1.0,inarray,1,f,1);
            }

            //----------------------------------
            //  Solve continuous flux System
            //----------------------------------
            int GloBndDofs   = m_traceMap->GetNumGlobalBndCoeffs();
            int NumDirichlet = m_traceMap->GetNumLocalDirBndCoeffs();
            int e_ncoeffs;

            // Retrieve block matrix of U^e
            GlobalMatrixKey HDGLamToUKey(StdRegions::eHybridDGLamToU,
                NullAssemblyMapSharedPtr,factors,varcoeff);
            const DNekScalBlkMatSharedPtr &HDGLamToU = GetBlockMatrix(
                HDGLamToUKey);


            // Retrieve number of local trace space coefficients N_{\lambda},
            // and set up local elemental trace solution \lambda^e.
            int     LocBndCoeffs = m_traceMap->GetNumLocalBndCoeffs();
            Array<OneD, NekDouble> bndrhs(LocBndCoeffs,0.0);
            Array<OneD, NekDouble> loclambda(LocBndCoeffs,0.0);
            DNekVec LocLambda(LocBndCoeffs,loclambda,eWrapper);

            //----------------------------------
            // Evaluate Trace Forcing vector F
            // Kirby et al, 2010, P23, Step 5.
            //----------------------------------
            // Loop over all expansions in the domain
            for(cnt = cnt1 = n = 0; n < nexp; ++n)
            {
                nbndry = (*m_exp)[n]->NumDGBndryCoeffs();

                e_ncoeffs = (*m_exp)[n]->GetNcoeffs();
                e_f       = f + cnt;
                e_l       = bndrhs + cnt1;

                // Local trace space \lambda^e
                DNekVec     Floc    (nbndry, e_l, eWrapper);
                // Local forcing f^e
                DNekVec     ElmtFce (e_ncoeffs, e_f, eWrapper);
                // Compute local (U^e)^{\top} f^e
                Floc = Transpose(*(HDGLamToU->GetBlock(n,n)))*ElmtFce;

                cnt   += e_ncoeffs;
                cnt1  += nbndry;
            }

            Array<OneD, const int> bndCondMap =  
                m_traceMap->GetBndCondCoeffsToLocalTraceMap();
            Array<OneD, const NekDouble> Sign = 
                m_traceMap->GetLocalToGlobalBndSign();

            // Copy Dirichlet boundary conditions and weak forcing
            // into trace space
            int locid;
            cnt = 0;
            for(i = 0; i < m_bndCondExpansions.size(); ++i)
            {
                Array<OneD, const NekDouble> bndcoeffs =
                    m_bndCondExpansions[i]->GetCoeffs();
                
                if(m_bndConditions[i]->GetBoundaryConditionType() ==
                       SpatialDomains::eDirichlet)
                {
                    for(j = 0; j < (m_bndCondExpansions[i])->GetNcoeffs(); ++j)
                    {
                        locid = bndCondMap[cnt + j];
                        loclambda[locid] = Sign[locid]*bndcoeffs[j]; 
                    }
                }
                else if (m_bndConditions[i]->GetBoundaryConditionType() ==
                             SpatialDomains::eNeumann ||
                         m_bndConditions[i]->GetBoundaryConditionType() ==
                             SpatialDomains::eRobin)
                {
                    //Add weak boundary condition to trace forcing
                    for(j = 0; j < (m_bndCondExpansions[i])->GetNcoeffs(); ++j)
                    {
                        locid = bndCondMap[cnt + j];
                        bndrhs[locid] += Sign[locid]*bndcoeffs[j]; 
                    }
                }
                else if (m_bndConditions[i]->GetBoundaryConditionType() ==
                             SpatialDomains::ePeriodic)
                {
                    ASSERTL0(false, "HDG implementation does not support "
                             "periodic boundary conditions at present.");
                }
                cnt += (m_bndCondExpansions[i])->GetNcoeffs();
            }

            //----------------------------------
            // Solve trace problem: \Lambda = K^{-1} F
            // K is the HybridDGHelmBndLam matrix.
            //----------------------------------
            if(GloBndDofs - NumDirichlet > 0)
            {
                GlobalLinSysKey       key(StdRegions::eHybridDGHelmBndLam,
                                          m_traceMap,factors,varcoeff);
                GlobalLinSysSharedPtr LinSys = GetGlobalBndLinSys(key);
                LinSys->Solve(bndrhs,loclambda,m_traceMap);

                // For consistency with previous version put global
                // solution into m_trace->m_coeffs
                m_traceMap->LocalToGlobal(loclambda,m_trace->UpdateCoeffs());
            }
                
            //----------------------------------
            // Internal element solves
            //----------------------------------
            GlobalMatrixKey invHDGhelmkey(StdRegions::eInvHybridDGHelmholtz,
                NullAssemblyMapSharedPtr,factors,varcoeff);
            const DNekScalBlkMatSharedPtr& InvHDGHelm = GetBlockMatrix(
                invHDGhelmkey);
            DNekVec out(m_ncoeffs,outarray,eWrapper);
            Vmath::Zero(m_ncoeffs,outarray,1);

            //  out =  u_f + u_lam = (*InvHDGHelm)*f + (LamtoU)*Lam
            out = (*InvHDGHelm)*F + (*HDGLamToU)*LocLambda;
        }


        /**
         * @brief Calculates the result of the multiplication of a global matrix
         * of type specified by @a mkey with a vector given by @a inarray.
         *
         * @param mkey      Key representing desired matrix multiplication.
         * @param inarray   Input vector.
         * @param outarray  Resulting multiplication.
         */
        void DisContField2D::v_GeneralMatrixOp(
               const GlobalMatrixKey             &gkey,
               const Array<OneD,const NekDouble> &inarray,
               Array<OneD,      NekDouble> &outarray)
        {
            int     LocBndCoeffs = m_traceMap->GetNumLocalBndCoeffs();
            Array<OneD, NekDouble> loc_lambda(LocBndCoeffs);
            DNekVec LocLambda(LocBndCoeffs,loc_lambda,eWrapper);
            const DNekScalBlkMatSharedPtr& HDGHelm = GetBlockMatrix(gkey);

            m_traceMap->GlobalToLocalBnd(inarray, loc_lambda);
            LocLambda = (*HDGHelm) * LocLambda;
            m_traceMap->AssembleBnd(loc_lambda,outarray);
        }

        /**
         * @brief Search through the edge expansions and identify which ones
         * have Robin/Mixed type boundary conditions.
         *
         * If a Robin boundary is found then store the edge ID of the boundary
         * condition and the array of points of the physical space boundary
         * condition which are hold the boundary condition primitive variable
         * coefficient at the quatrature points
         *
         * @return A map containing the Robin boundary condition information
         *         using a key of the element ID.
         */
        map<int, RobinBCInfoSharedPtr> DisContField2D::v_GetRobinBCInfo(void)
        {
            int i,cnt;
            map<int, RobinBCInfoSharedPtr> returnval;
            Array<OneD, int> ElmtID,EdgeID;
            GetBoundaryToElmtMap(ElmtID,EdgeID);

            for(cnt = i = 0; i < m_bndCondExpansions.size(); ++i)
            {
                MultiRegions::ExpListSharedPtr locExpList;

                if(m_bndConditions[i]->GetBoundaryConditionType() ==
                       SpatialDomains::eRobin)
                {
                    int e,elmtid;
                    Array<OneD, NekDouble> Array_tmp;

                    locExpList = m_bndCondExpansions[i];

                    int npoints    = locExpList->GetNpoints();
                    Array<OneD, NekDouble> x0(npoints, 0.0);
                    Array<OneD, NekDouble> x1(npoints, 0.0);
                    Array<OneD, NekDouble> x2(npoints, 0.0);
                    Array<OneD, NekDouble> coeffphys(npoints);

                    locExpList->GetCoords(x0, x1, x2);

                    LibUtilities::Equation coeffeqn =
                        std::static_pointer_cast<
                            SpatialDomains::RobinBoundaryCondition>
                        (m_bndConditions[i])->m_robinPrimitiveCoeff;

                    // evalaute coefficient
                    coeffeqn.Evaluate(x0, x1, x2, 0.0, coeffphys);

                    for(e = 0; e < locExpList->GetExpSize(); ++e)
                    {
                        RobinBCInfoSharedPtr rInfo =
                            MemoryManager<RobinBCInfo>
                            ::AllocateSharedPtr(
                                EdgeID[cnt+e],
                                Array_tmp = coeffphys +
                                locExpList->GetPhys_Offset(e));

                        elmtid = ElmtID[cnt+e];
                        // make link list if necessary
                        if(returnval.count(elmtid) != 0)
                        {
                            rInfo->next = returnval.find(elmtid)->second;
                        }
                        returnval[elmtid] = rInfo;
                    }
                }
                cnt += m_bndCondExpansions[i]->GetExpSize();
            }

            return returnval;
        }

        /**
         * @brief Evaluate HDG post-processing to increase polynomial order of
         * solution.
         *
         * This function takes the solution (assumed to be one order lower) in
         * physical space, and postprocesses at the current polynomial order by
         * solving the system:
         *
         * \f[
         * \begin{aligned}
         *   (\nabla w, \nabla u^*) &= (\nabla w, u), \\
         *   \langle \nabla u^*, 1 \rangle &= \langle \nabla u, 1 \rangle
         * \end{aligned}
         * \f]
         *
         * where \f$ u \f$ corresponds with the current solution as stored
         * inside #m_coeffs.
         *
         * @param outarray  The resulting field \f$ u^* \f$.
         */
        void  DisContField2D::EvaluateHDGPostProcessing(
            Array<OneD, NekDouble> &outarray)
        {
            int    i,cnt,e,ncoeff_edge;
            Array<OneD, NekDouble> force, out_tmp, qrhs, qrhs1;
            Array<OneD, Array< OneD, LocalRegions::ExpansionSharedPtr> >
                &elmtToTrace = m_traceMap->GetElmtToTrace();

            StdRegions::Orientation edgedir;

            int     nq_elmt, nm_elmt;
            int     LocBndCoeffs = m_traceMap->GetNumLocalBndCoeffs();
            Array<OneD, NekDouble> loc_lambda(LocBndCoeffs), edge_lambda;
            Array<OneD, NekDouble> tmp_coeffs;
            m_traceMap->GlobalToLocalBnd(m_trace->GetCoeffs(),loc_lambda);

            edge_lambda = loc_lambda;

            // Calculate Q using standard DG formulation.
            for(i = cnt = 0; i < GetExpSize(); ++i)
            {
                nq_elmt = (*m_exp)[i]->GetTotPoints();
                nm_elmt = (*m_exp)[i]->GetNcoeffs();
                qrhs  = Array<OneD, NekDouble>(nq_elmt);
                qrhs1  = Array<OneD, NekDouble>(nq_elmt);
                force = Array<OneD, NekDouble>(2*nm_elmt);
                out_tmp = force + nm_elmt;
                LocalRegions::ExpansionSharedPtr ppExp;

                int num_points0 = (*m_exp)[i]->GetBasis(0)->GetNumPoints();
                int num_points1 = (*m_exp)[i]->GetBasis(1)->GetNumPoints();
                int num_modes0 = (*m_exp)[i]->GetBasis(0)->GetNumModes();
                int num_modes1 = (*m_exp)[i]->GetBasis(1)->GetNumModes();

                // Probably a better way of setting up lambda than this.  Note
                // cannot use PutCoeffsInToElmts since lambda space is mapped
                // during the solve.
                int nEdges = (*m_exp)[i]->GetNedges();
                Array<OneD, Array<OneD, NekDouble> > edgeCoeffs(nEdges);

                for(e = 0; e < (*m_exp)[i]->GetNedges(); ++e)
                {
                    edgedir = (*m_exp)[i]->GetEorient(e);
                    ncoeff_edge = elmtToTrace[i][e]->GetNcoeffs();
                    edgeCoeffs[e] = Array<OneD, NekDouble>(ncoeff_edge);
                    Vmath::Vcopy(ncoeff_edge, edge_lambda, 1, edgeCoeffs[e], 1);
                    elmtToTrace[i][e]->SetCoeffsToOrientation(
                        edgedir, edgeCoeffs[e], edgeCoeffs[e]);
                    edge_lambda = edge_lambda + ncoeff_edge;
                }

                //creating orthogonal expansion (checking if we have quads or triangles)
                LibUtilities::ShapeType shape = (*m_exp)[i]->DetShapeType();
                switch(shape)
                {
                    case LibUtilities::eQuadrilateral:
                    {
                        const LibUtilities::PointsKey PkeyQ1(num_points0,LibUtilities::eGaussLobattoLegendre);
                        const LibUtilities::PointsKey PkeyQ2(num_points1,LibUtilities::eGaussLobattoLegendre);
                        LibUtilities::BasisKey  BkeyQ1(LibUtilities::eOrtho_A, num_modes0, PkeyQ1);
                        LibUtilities::BasisKey  BkeyQ2(LibUtilities::eOrtho_A, num_modes1, PkeyQ2);
                        SpatialDomains::QuadGeomSharedPtr qGeom = std::dynamic_pointer_cast<SpatialDomains::QuadGeom>((*m_exp)[i]->GetGeom());
                        ppExp = MemoryManager<LocalRegions::QuadExp>::AllocateSharedPtr(BkeyQ1, BkeyQ2, qGeom);
                    }
                    break;
                    case LibUtilities::eTriangle:
                    {
                        const LibUtilities::PointsKey PkeyT1(num_points0,LibUtilities::eGaussLobattoLegendre);
                        const LibUtilities::PointsKey PkeyT2(num_points1,LibUtilities::eGaussRadauMAlpha1Beta0);
                        LibUtilities::BasisKey  BkeyT1(LibUtilities::eOrtho_A, num_modes0, PkeyT1);
                        LibUtilities::BasisKey  BkeyT2(LibUtilities::eOrtho_B, num_modes1, PkeyT2);
                        SpatialDomains::TriGeomSharedPtr tGeom = std::dynamic_pointer_cast<SpatialDomains::TriGeom>((*m_exp)[i]->GetGeom());
                        ppExp = MemoryManager<LocalRegions::TriExp>::AllocateSharedPtr(BkeyT1, BkeyT2, tGeom);
                    }
                    break;
                    default:
                        ASSERTL0(false, "Wrong shape type, HDG postprocessing is not implemented");
                };


                //DGDeriv
                // (d/dx w, d/dx q_0)
                (*m_exp)[i]->DGDeriv(
                    0,tmp_coeffs = m_coeffs + m_coeff_offset[i],
                    elmtToTrace[i], edgeCoeffs, out_tmp);
                (*m_exp)[i]->BwdTrans(out_tmp,qrhs);
                //(*m_exp)[i]->IProductWRTDerivBase(0,qrhs,force);
                ppExp->IProductWRTDerivBase(0,qrhs,force);


                // + (d/dy w, d/dy q_1)
                (*m_exp)[i]->DGDeriv(
                    1,tmp_coeffs = m_coeffs + m_coeff_offset[i],
                    elmtToTrace[i], edgeCoeffs, out_tmp);

                (*m_exp)[i]->BwdTrans(out_tmp,qrhs);
                //(*m_exp)[i]->IProductWRTDerivBase(1,qrhs,out_tmp);
                ppExp->IProductWRTDerivBase(1,qrhs,out_tmp);

                Vmath::Vadd(nm_elmt,force,1,out_tmp,1,force,1);

                // determine force[0] = (1,u)
                (*m_exp)[i]->BwdTrans(
                    tmp_coeffs = m_coeffs + m_coeff_offset[i],qrhs);
                force[0] = (*m_exp)[i]->Integral(qrhs);

                // multiply by inverse Laplacian matrix
                // get matrix inverse
                LocalRegions::MatrixKey  lapkey(StdRegions::eInvLaplacianWithUnityMean, ppExp->DetShapeType(), *ppExp);
                DNekScalMatSharedPtr lapsys = ppExp->GetLocMatrix(lapkey);

                NekVector<NekDouble> in (nm_elmt,force,eWrapper);
                NekVector<NekDouble> out(nm_elmt);

                out = (*lapsys)*in;

                // Transforming back to modified basis
                Array<OneD, NekDouble> work(nq_elmt);
                ppExp->BwdTrans(out.GetPtr(), work);
                (*m_exp)[i]->FwdTrans(work, tmp_coeffs = outarray + m_coeff_offset[i]);
            }
        }
       
        /**
         * Evaluates the boundary condition expansions, \a bndCondExpansions,
         * given the information provided by \a bndConditions.
         * @param   time        The time at which the boundary conditions
         *                      should be evaluated.
         * @param   bndCondExpansions   List of boundary conditions.
         * @param   bndConditions   Information about the boundary conditions.
         *
         * This will only be undertaken for time dependent
         * boundary conditions unless time == 0.0 which is the
         * case when the method is called from the constructor.
         */
        void DisContField2D::v_EvaluateBoundaryConditions(
            const NekDouble   time,
            const std::string varName,
            const NekDouble   x2_in,
            const NekDouble   x3_in)
        {
            boost::ignore_unused(x3_in);

            int i;
            int npoints;
            int nbnd = m_bndCondExpansions.size();
        

            MultiRegions::ExpListSharedPtr locExpList;

            for (i = 0; i < nbnd; ++i)
            {
                if (time == 0.0 ||
                    m_bndConditions[i]->IsTimeDependent())
                {
                   
                    m_bndCondBndWeight[i] = 1.0;
                    locExpList = m_bndCondExpansions[i];
                    npoints    = locExpList->GetNpoints();
                    Array<OneD, NekDouble> x0(npoints, 0.0);
                    Array<OneD, NekDouble> x1(npoints, 0.0);
                    Array<OneD, NekDouble> x2(npoints, 0.0);

                    // Homogeneous input case for x2.
                    if (x2_in == NekConstants::kNekUnsetDouble)
                    {
                        locExpList->GetCoords(x0, x1, x2);
                    }
                    else
                    {
                        locExpList->GetCoords(x0, x1, x2);
                        Vmath::Fill(npoints, x2_in, x2, 1);
                    }

                    if (m_bndConditions[i]->GetBoundaryConditionType()
                        == SpatialDomains::eDirichlet)
                    {
                        
                        SpatialDomains::DirichletBCShPtr bcPtr
                            = std::static_pointer_cast<
                                SpatialDomains::DirichletBoundaryCondition>(
                                    m_bndConditions[i]);
                        string filebcs = bcPtr->m_filename;

                        if (filebcs != "")
                        {
                            ExtractFileBCs(filebcs, bcPtr->GetComm(), varName, locExpList);
                        }
                        else
                        {
                            LibUtilities::Equation condition =
                                std::static_pointer_cast<
                                    SpatialDomains::DirichletBoundaryCondition>
                                        (m_bndConditions[i])->
                                            m_dirichletCondition;

                            condition.Evaluate(x0, x1, x2, time,
                                               locExpList->UpdatePhys());
                        }

                        locExpList->FwdTrans_BndConstrained(
                            locExpList->GetPhys(),
                            locExpList->UpdateCoeffs());
                    }
                    else if (m_bndConditions[i]->GetBoundaryConditionType()
                             == SpatialDomains::eNeumann)
                    {
                        SpatialDomains::NeumannBCShPtr bcPtr = std::static_pointer_cast<
                                SpatialDomains::NeumannBoundaryCondition>(
                                        m_bndConditions[i]);
                        string filebcs  = bcPtr->m_filename;
                        if (filebcs != "")
                        {
                            ExtractFileBCs(filebcs, bcPtr->GetComm(), varName, locExpList);
                        }
                        else
                        {
                            LibUtilities::Equation condition =
                                std::static_pointer_cast<
                                    SpatialDomains::NeumannBoundaryCondition>
                                        (m_bndConditions[i])->
                                            m_neumannCondition;
                            condition.Evaluate(x0, x1, x2, time,
                                               locExpList->UpdatePhys());
                        }

                        locExpList->IProductWRTBase(
                            locExpList->GetPhys(),
                            locExpList->UpdateCoeffs());
                    }
                    else if (m_bndConditions[i]->GetBoundaryConditionType()
                             == SpatialDomains::eRobin)
                    {
                        SpatialDomains::RobinBCShPtr bcPtr = std::static_pointer_cast<
                            SpatialDomains::RobinBoundaryCondition>
                                (m_bndConditions[i]);
                        string filebcs = bcPtr->m_filename;

                        if (filebcs != "")
                        {
                            ExtractFileBCs(filebcs, bcPtr->GetComm(), varName, locExpList);
                        }
                        else
                        {
                            LibUtilities::Equation condition =
                                std::static_pointer_cast<
                                    SpatialDomains::RobinBoundaryCondition>
                                        (m_bndConditions[i])->
                                            m_robinFunction;
                            condition.Evaluate(x0, x1, x2, time,
                                               locExpList->UpdatePhys());
                        }

                        locExpList->IProductWRTBase(
                            locExpList->GetPhys(),
                            locExpList->UpdateCoeffs());
                    }
                    else if (m_bndConditions[i]->GetBoundaryConditionType()
                             == SpatialDomains::ePeriodic)
                    {
                        continue;
                    }
                    else
                    {
                        ASSERTL0(false, "This type of BC not implemented yet");
                    }
                }
            }
        }
    } // end of namespace
} //end of namespace
