#include <comp.hpp>
#include <hdivlofe.hpp>  
#include <hdivhofe.hpp>  
#include <hdivhofefo.hpp>  

#include "hdivhosurfacefespace.hpp"  

namespace ngcomp
{

  HDivHighOrderSurfaceFESpace ::  
  HDivHighOrderSurfaceFESpace (shared_ptr<MeshAccess> ama, const Flags & flags, bool parseflags)
    : FESpace (ama, flags)
  {
    type = "hdivhosurface";
    name="HDivHighOrderSurfaceFESpace(hdivhosurf)";
       
    DefineDefineFlag("discontinuous");   
    DefineDefineFlag("hodivfree"); 
    
    if(parseflags) CheckFlags(flags);

    discont = flags.GetDefineFlag("discontinuous"); 

    order =  int (flags.GetNumFlag ("order",0));       

    if (flags.NumFlagDefined("order")) 
      {
	order =  int (flags.GetNumFlag ("order",0));
      }
    else 
      {       
	order = 0;  
      }
    
    ho_div_free = flags.GetDefineFlag("hodivfree"); 
           
    auto one = make_shared<ConstantCoefficientFunction> (1);
    
    if (ma->GetDimension() == 2)
      {
	throw Exception ("only 2D manifolds supported");       
      }
    else
      {
	
	evaluator[BND] = make_shared<T_DifferentialOperator<DiffOpIdVecHDivBoundary<3>>>();
      }
    
    highest_order_dc = flags.GetDefineFlag("highest_order_dc");
    if (highest_order_dc) {
      *testout << "highest_order_dc is active!" << endl;
    }
  }
  
  HDivHighOrderSurfaceFESpace:: ~HDivHighOrderSurfaceFESpace () 
  {
    ;
  }


  void HDivHighOrderSurfaceFESpace :: Update(LocalHeap & lh)
  {
    FESpace::Update(lh);

    size_t nel = ma->GetNSE();
    size_t nfa = ma->GetNEdges();
    size_t dim = ma->GetDimension();
       
    first_facet_dof.SetSize(nfa+1);
    first_inner_dof.SetSize(nel+1);
    
    /////////////// old ///////////////
    /*
    order_facet = pc;
    order_inner = p;
    fine_facet = 0; //!!!! 


    for (auto el : ma->Elements(VOL))
      {
        if (!DefinedOn (el))
          {
            order_inner[el.Nr()] = 0;
            order_inner_curl[el.Nr()] = 0;
            continue;
          }
          
	ELEMENT_TYPE eltype = el.GetType();
	const POINT3D * points = ElementTopology :: GetVertices (eltype);
	
	// Array<int> elfacets; 
	// ma->GetElFacets (el.Nr(), elfacets); 
	// auto elfacets = ma->GetElFacets (el);
        auto elfacets = el.Facets();
        
        fine_facet[elfacets] = true;
	
	if(!var_order) continue; 
	
	INT<3> el_orders = ma->GetElOrders(el.Nr());  
	
        int i = el.Nr();
	for(int k=0;k<dim;k++)
	  {
	    order_inner_curl[i][k]= max2(el_orders[k] + rel_curl_order,0);
	    order_inner[i][k] = max2(el_orders[k]+rel_order,0);
	  }

	if(dim==2)
	  {
	    const EDGE * edges = ElementTopology::GetEdges (eltype);
	    for(int j=0; j<elfacets.Size(); j++)
	      for(int k=0;k<2;k++)
		if(points[edges[j][0]][k] != points[edges[j][1]][k])
		  { 
		    order_facet[elfacets[j]][0] = max2(el_orders[k]+rel_curl_order, order_facet[elfacets[j]][0]);
		    break; 
		  }
	  }
      }

    
    if(print) 
      {
	*testout << " discont " << discont << endl;
	*testout << " fine_facet[i] (hdiv) " << fine_facet << endl; 
	
	*testout << " order_facet (hdivho) " << order_facet << endl; 
	*testout << " order_inner (hdivho) " << order_inner << endl; 	
      }
    */
    UpdateDofTables(); 
    //UpdateCouplingDofArray();
  }

  void HDivHighOrderSurfaceFESpace :: UpdateDofTables()
  {
    size_t nel = ma->GetNSE();
    size_t nfa = ma->GetNEdges();
    size_t dim = ma->GetDimension();
    
    ndof = nfa;
    first_facet_dof = ndof;

    if(dim==3)
      {
	for (auto i : Range(nfa))
          {
            first_facet_dof[i] = ndof;
            //int inc = fine_facet[i] ? order_facet[i][0] : 0;
	    //if (highest_order_dc && !boundary_facet[i]) inc--;
            //if (inc > 0) ndof += inc;            
          }

        first_facet_dof[nfa] = ndof;
      
        for (size_t i = 0; i < nel; i++)
          {
            ElementId ei(BND, i);
            //INT<3> pc = order_inner_curl[i];
            //INT<3> p = order_inner[i];
            int inci = 0;
            switch(ma->GetElType(ei))
              {
              case ET_TRIG:
                if (!ho_div_free)
		  inci = order*(order-1)/2+order*(order-1)/2+order-1;
		//inci = pc[0]*(pc[0]-1)/2 + p[0]*(p[0]-1)/2 + p[0]-1;
                else
		  inci = order*(order-1)/2;
		    //inci = pc[0]*(pc[0]-1)/2;
                break;
              case ET_QUAD:
		throw Exception("not implemented yet");
                //if (!ho_div_free)
                //  inci = pc[0]*pc[1] + p[0]*p[1]+p[0]+p[1];
                //else
                //  inci = pc[0]*pc[1];
                //break;
              default: // for the compiler
                break;  
              }

	    if (highest_order_dc)
	      {
                /*
		ma->GetElFacets (ei, fnums);
                for (auto f : fnums)
		  if (!boundary_facet[f]) inci++;
                */
                for (auto f : ma->GetElFacets(ei))
		  if (!boundary_facet[f]) inci++;
	      }

            first_inner_dof[i] = ndof;
            if (inci > 0) ndof+=inci;
          }
        first_inner_dof[nel] = ndof;


        if (highest_order_dc)
          {
            dc_pairs.SetSize (ma->GetNFacets());
            dc_pairs = INT<2> (-1,-1);
            
            // Array<int> fnums;
            for (auto ei : ma->Elements(VOL))
              {
                // auto i = ei.Nr();
                // ma->GetElFacets (ei, fnums);
                // auto fnums = ma->GetElFacets(ei);
		int fid = first_inner_dof[ei.Nr()];
                for (auto f : ma->GetElFacets(ei))
		  if (!boundary_facet[f])
		    {
		      int di = fid++; // first_inner_dof[i]+k;
		      dc_pairs[f][1] = dc_pairs[f][0];
		      dc_pairs[f][0] = di;
		    }
              }
          }
        else
          dc_pairs.SetSize0 ();
      }
    else
      {
	throw Exception ("you should not be here - only 2D manifolds supported");
      }    
        
    if(print) 
      {
        (*testout) << "ndof hdivhofespace update = " << endl << ndof << endl;        
        (*testout) << "first_facet_dof (hdiv) = " << endl << first_facet_dof << endl;
        (*testout) << "first_inner_dof (hdiv) = " << endl << first_inner_dof << endl;
      }
  }

  void HDivHighOrderSurfaceFESpace :: UpdateCouplingDofArray()
  {
    ctofdof.SetSize(ndof);
    if(discont) 
      {
        ctofdof = LOCAL_DOF;
        return;
      } 
    
    ctofdof = WIREBASKET_DOF;
    
    for (auto facet : Range(ma->GetNFacets()))
      {
        ctofdof[facet] = fine_facet[facet] ? WIREBASKET_DOF : UNUSED_DOF;
        ctofdof[GetFacetDofs(facet)] = INTERFACE_DOF;
      }
    
    for (auto el : Range (ma->GetNE()))
      ctofdof[GetElementDofs(el)] = LOCAL_DOF;
  }


  FiniteElement & HDivHighOrderSurfaceFESpace :: GetFE (ElementId ei, Allocator & alloc) const
  {
    throw Exception("No volume elements available");
  }

 FiniteElement & HDivHighOrderSurfaceFESpace :: GetSFE (ElementId ei, Allocator & alloc) const
  {
    throw Exception("No surface elements available");
  }
  
  const FiniteElement & HDivHighOrderSurfaceFESpace :: GetHODivFE (int elnr, LocalHeap & lh) const
  {
    Ngs_Element ngel = ma->GetElement(ElementId(VOL,elnr));
    ELEMENT_TYPE eltype = ngel.GetType();
    
    if (!ho_div_free) throw Exception ("You don't have hodivfree active. You are not allow to call GetHODivFE");
    
    switch (eltype)
      {        
	//case ET_TRIG:    return T_GetFE<ET_TRIG> (elnr, true, lh);
	//case ET_QUAD:    return T_GetFE<ET_QUAD> (elnr, true, lh);       	
        
      default:
        throw Exception ("illegal element in HDivHOFeSpace::GetDivFE");
      }
  }
  
  size_t HDivHighOrderSurfaceFESpace :: GetNDof () const throw()
  {
    return ndof;
  }

  void HDivHighOrderSurfaceFESpace :: GetDofNrs (ElementId ei, Array<int> & dnums) const
  {
    dnums.SetSize0();
  }

  void HDivHighOrderSurfaceFESpace :: GetSDofNrs (ElementId ei, Array<int> & dnums) const
  {
    dnums.SetSize0();
    
    if(ei.VB()==BND)
      {
	auto fanums = ma->GetElEdges(ei);
	
	if (highest_order_dc)
	  {
	    IntRange eldofs = GetElementDofs (ei.Nr());
	    
	    dnums += fanums;
		
	    int first_el_dof = eldofs.First();
	    for (auto f : fanums)
	      {
		dnums += GetFacetDofs (f);
		if (!boundary_facet[f])
		  dnums += first_el_dof++;
	      }
	    dnums += IntRange (first_el_dof, eldofs.Next());
	  }	         
	else
	  {
	    //lowest order RT0 dofs
            dnums += fanums;
	    
	    // edges
	    for (auto f : fanums)
	      dnums += GetFacetDofs (f);
	    
	    //inner
	    dnums += GetElementDofs (ei.Nr());
	  }
	
	if (!DefinedOn (ei))
          dnums.SetSize0();
      }
    else if (ei.VB()==BBND)
      {
	throw Exception("GetSDofNrs for BBND not implemented yet");
	  /*
	 size_t fanum = ma->GetElFacets(ei)[0];
	// lowest-order
        dnums += fanum;
	
	// high order
        dnums += GetFacetDofs (fanum);
        
	if (!DefinedOn (ei))
          dnums.SetSize0();
        // dnums = -1;
	*/
	  }
  }

  void HDivHighOrderSurfaceFESpace :: GetVertexDofNrs (int vnr, Array<int> & dnums) const
  {
    dnums.SetSize0(); 
  }
  
  void HDivHighOrderSurfaceFESpace :: GetFacetDofNrs (int fanr, Array<int> & dnums) const
  {
    dnums.SetSize0();
    if(ma->GetDimension() == 2 || discont) return; 

    dnums += fanr;
    //high order facet dofs
    dnums += GetFacetDofs (fanr);
  }

  void HDivHighOrderSurfaceFESpace :: GetInnerDofNrs (int elnr, Array<int> & dnums) const
  {
    dnums = GetElementDofs (elnr);
  }

  SymbolTable<shared_ptr<DifferentialOperator>>
  HDivHighOrderSurfaceFESpace :: GetAdditionalEvaluators () const
  {
    SymbolTable<shared_ptr<DifferentialOperator>> additional;
    /*
    switch (ma->GetDimension())
      {
      case 1:
        additional.Set ("grad", make_shared<T_DifferentialOperator<DiffOpGradientHdiv<1>>> ()); break;
      case 2:
        additional.Set ("grad", make_shared<T_DifferentialOperator<DiffOpGradientHdiv<2>>> ()); break;
      case 3:
        additional.Set ("grad", make_shared<T_DifferentialOperator<DiffOpGradientHdiv<3>>> ()); break;
      default:
        ;
	}*/
    return additional;
  }
  

  static RegisterFESpace<HDivHighOrderSurfaceFESpace> init ("hdivhosurface");
}



