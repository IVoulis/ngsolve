/*********************************************************************/
/* File:   cuda_linalg.cpp                                           */
/* Author: Joachim Schoeberl, Matthias Hochsteger                    */
/* Date:   11. Aug. 2014                                             */
/*********************************************************************/


#include <la.hpp>
#include <cublas_v2.h>
#include <cusparse.h>
#include <cuda_runtime_api.h>

#include "cuda_linalg.hpp"

/* extern void SetScalar (double val, int n, double * dev_ptr); */

namespace ngla
{

  cublasHandle_t Get_CuBlas_Handle ()
  {
    static Timer tblashandle("CUDA create cublas handle");
    RegionTimer reg(tblashandle);

    static cublasHandle_t handle;
    static bool first_call = true;

    if (first_call)
      {
        first_call = false;
        cublasCreate (&handle);
      }
    return handle;
  }
  cusparseHandle_t Get_CuSparse_Handle ()
  {
    static Timer tsparsehandle("CUDA create cusparse handle");
    RegionTimer reg(tsparsehandle);

    static cusparseHandle_t handle;
    static bool first_call = true;

    if (first_call)
      {
        first_call = false;
        cusparseCreate (&handle);
      }
    return handle;
  }

  void InitCuLinalg()
  {
    cerr << "Initializing cublas and cusparse." << endl;
    Get_CuBlas_Handle();
    Get_CuSparse_Handle();

    BaseMatrix::RegisterDeviceMatrixCreator(typeid(SparseMatrix<double>),
                                            [] (const BaseMatrix & mat) -> shared_ptr<BaseMatrix>
                                            {
                                              auto & sparse_mat = dynamic_cast<const SparseMatrix<double>&>(mat);
                                              return make_shared<DevSparseMatrix>(sparse_mat);
                                            });
  }

  /******************** Unified Vector ********************/

  UnifiedVector :: UnifiedVector (int asize)
  {
    this->size = asize;
    cout << IM(7) << "Create unified vector, size = " << size << endl;

    host_data = new double[size];
    auto err = cudaMalloc((void**)&dev_data, size*sizeof(double));
    if (err != 0)
      throw Exception("UnifiedVector allocation error, ec="+ToString(err));
    
    cusparseCreateDnVec (&descr, size, dev_data, CUDA_R_64F);

    host_uptodate = false;
    dev_uptodate = false;
  }

  UnifiedVector :: UnifiedVector (const BaseVector& vec) : UnifiedVector(vec.Size())
  {
    (*this) = vec;
  }

  UnifiedVector :: ~UnifiedVector ()
  {
    /* cerr << "dtor UnifiedVector" << endl; */

    cusparseDestroyDnVec(descr);
    cudaFree(dev_data);
    delete[] host_data;
  }

  BaseVector & UnifiedVector :: operator= (double d)
  {
    /* for (int i = 0; i < size; i++) host_data[i] = d; */
    host_uptodate = false;

    /* ::SetScalar (d, size, dev_data); */
    cublasDscal(Get_CuBlas_Handle(), size, &d, dev_data, 1); 
    dev_uptodate = true;
    
    return *this;
    /*
    host_uptodate = true;
    dev_uptodate = false;
    UpdateDevice();
    return *this;
    */
  }

  BaseVector & UnifiedVector :: operator= (const BaseVector & v2)
  {
    const UnifiedVector * uv2 = dynamic_cast<const UnifiedVector*> (&v2);
    if (uv2)
      {
        if (uv2->dev_uptodate)
          {
            cudaMemcpy (dev_data, uv2->dev_data, sizeof(double)*size, cudaMemcpyDeviceToDevice);    
            dev_uptodate = true;
            host_uptodate = false;
          }
        else if (uv2->host_uptodate)
          {
            FVDouble() = uv2->FVDouble();
            host_uptodate = true;
            dev_uptodate = false;
          }
        else
          {
            cerr << "operator= (BaseVector) : undefined vector" << endl;
          }
        return *this;
      }

    /* VFlatVector<double> fv(size, host_data); */
    /* fv = 1.0*v2; */
    FVDouble() = v2.FVDouble();

    host_uptodate = true;
    dev_uptodate = false;
    return *this;
  }

  const double & UnifiedVector :: operator [] (const int ind) const
  {
    /* cerr << "UnifiedVector operator[]" << endl; */
    UpdateHost(); 
    return host_data[ind];
  }

  double & UnifiedVector :: operator [] (const int ind)
  {
    UpdateHost(); 
    dev_uptodate = false;
    return host_data[ind];
  }

  const cusparseDnVecDescr_t& UnifiedVector :: GetDescr() const
  {
    return descr;
  }

  cusparseDnVecDescr_t& UnifiedVector :: GetDescr()
  {
    return descr;
  }


  
  BaseVector & UnifiedVector :: Scale (double scal)
  {
    UpdateDevice();
    cublasDscal (Get_CuBlas_Handle(), size, &scal, dev_data, 1);
    host_uptodate = false;
    return *this;
  }

  BaseVector & UnifiedVector :: SetScalar (double scal)
  {
    (*this) = scal;
    return *this;
  }
  
  BaseVector & UnifiedVector :: Set (double scal, const BaseVector & v)
  {
    (*this) = 0.0;
    Add (scal, v);
    return *this;
  }
  
  
  BaseVector & UnifiedVector :: Add (double scal, const BaseVector & v)
  {
    const UnifiedVector * v2 = dynamic_cast<const UnifiedVector*> (&v);

    if (v2)
      {
        UpdateDevice();
        v2->UpdateDevice();

        cublasDaxpy (Get_CuBlas_Handle(), 
                           size, &scal, v2->dev_data, 1, dev_data, 1);
        host_uptodate = false;
      }
    else
      {
        FVDouble() += scal * v.FVDouble();
      }

    return *this;
  }
  
  double UnifiedVector :: InnerProduct (const BaseVector & v2, bool conjugate) const
  {
    if (conjugate)
      throw Exception("conjugate in innerproduct not implemented yet.");

    static Timer tdot("CUDA InnerProduct");
    RegionTimer reg(tdot);

    const UnifiedVector * uv2 = dynamic_cast<const UnifiedVector*> (&v2);
    if (uv2)
    {
      static Timer tdot("CUDA InnerProduct devdev");
      RegionTimer reg(tdot);
      UpdateDevice();
      uv2->UpdateDevice();
      
      double res;
      cublasDdot (Get_CuBlas_Handle(), 
                        size, dev_data, 1, uv2->dev_data, 1, &res);
      return res;
    }

    FlatVector<> fv = FVDouble();
    FlatVector<> fv2 = v2.FVDouble();
    return ngbla::InnerProduct (fv, fv2);
  }


  ostream & UnifiedVector :: Print (ostream & ost) const
  {
    cout << "output unified vector of size " << size;
    cout << ", host = " << host_uptodate << ", dev = " << dev_uptodate << endl;
    if (!host_uptodate)
    {
      if (dev_uptodate)
      {
        cout << "host not up-to-data. printing device data" << endl;
        Vector<double> tmp(size);
        cudaMemcpy(tmp.Data(), dev_data, size * sizeof(double), cudaMemcpyDeviceToHost);
        ost << tmp << endl;
      }
      else
      {
        cout << "undefined vector" << endl;
      }
    }
    else
    {
      ost << FVDouble();
    }
    return ost;
  }

  // TODO: maybe remove. mainly for testing
  ostream & UnifiedVector :: PrintStatus (ostream & ost) const
  {
    cout << "output unified vector of size " << size;
    cout << ", host = " << host_uptodate << ", dev = " << dev_uptodate << endl;
    return ost;
  }
  
  /* void UnifiedVector :: PrintDevice () const */
  /* { */
  /*   int DSIZE = size * sizeof(double); */
  /*   double *tmp = (double*) malloc(DSIZE); */
  /*   cudaMemcpy(tmp, dev_data, DSIZE, cudaMemcpyDeviceToHost); */
  /*   cout << "device up-to-date: " << dev_uptodate << endl; */
  /*   for (int i=0; i<size; i++) */
  /*     cout << tmp[i] << endl; */
  /* } */

  AutoVector UnifiedVector :: CreateVector () const
  {
    return make_unique<UnifiedVector> (size);
  }

  void UnifiedVector :: UpdateHost () const
  {
    if (host_uptodate)
      return;

    if (dev_uptodate)
      cudaMemcpy (host_data, dev_data, sizeof(double)*size, cudaMemcpyDeviceToHost);    
    /* else */
    /*   cout << "ERROR UnifiedVector::UpdateHost non is uptodate" << endl; */

    host_uptodate = true;
  }

  void UnifiedVector :: UpdateDevice () const
  {
    if (dev_uptodate)
      return;

    if (host_uptodate)
      cudaMemcpy (dev_data, host_data, sizeof(double)*size, cudaMemcpyHostToDevice);
    /* else */
    /*   cout << "ERROR UnifiedVector::UpdateDevice non is uptodate" << endl; */

    cout << "Host2Device copy!" << endl;
    
    dev_uptodate = true;
  }
  
  FlatVector<double> UnifiedVector :: FVDouble () const
  {
    UpdateHost();
    dev_uptodate = false;
    return FlatVector<> (size, host_data);
  }
  
  FlatVector<Complex> UnifiedVector :: FVComplex () const
  {
    throw Exception ("unified complex not yet supported");
  }
    
  void * UnifiedVector :: Memory() const throw()
  { 
    UpdateHost(); 
    return host_data;
  }

  
  void UnifiedVector :: GetIndirect (const FlatArray<int> & ind, 
            const FlatVector<double> & v) const
  {
    cout << "UnifiedVector :: GetIndirect not supported" << endl;
  }
  void UnifiedVector :: GetIndirect (const FlatArray<int> & ind, 
            const FlatVector<Complex> & v) const
  {
    cout << "UnifiedVector :: GetIndirect not supported" << endl;
  }



  /******************** DevMatrix ********************/

  shared_ptr<BaseMatrix> CreateDevMatrix (BaseMatrix & mat)
  {
    if (auto res = mat.CreateDeviceMatrix())
      return res;
    
    if (typeid(mat) == typeid(SparseMatrix<double>))
    {
      SparseMatrix<double>& sparse_mat = dynamic_cast<SparseMatrix<double>&>(mat);
      return make_shared<DevSparseMatrix>(sparse_mat);
      /* *this = DevSparseMatrix(sparse_mat); */
      /* return *this; */
    }
    else if (typeid(mat) == typeid(ConstantElementByElementMatrix))
    {
      ConstantElementByElementMatrix& ebe_mat = dynamic_cast<ConstantElementByElementMatrix&>(mat);
      return make_shared<DevEBEMatrix>(ebe_mat);
    }
    /* else if (typeid(mat) == typeid(JacobiPrecond<double>)) */
    /* { */
    /*   JacobiPrecond<double>& jac_mat = dynamic_cast<JacobiPrecond<double>&>(mat); */
    /*   return make_shared<DevJacobiPrecond>(jac_mat); */
    /* } */
    else
    {
      throw Exception(string("matrix type not supported: ") + typeid(mat).name());
    }
  }

  shared_ptr<BaseMatrix> CreateDevMatrix (Matrix<> & mat)
  {
    Matrix<double>& dmat = dynamic_cast<Matrix<double>&>(mat);
    return make_shared<DevDMatrix>(dmat);
  }



  /******************** DevSparseMatrix ********************/

  DevSparseMatrix :: DevSparseMatrix (const SparseMatrix<double> & mat)
  {
    height = mat.Height();
    width = mat.Width();
    nze = mat.NZE();

    cout << IM(7) << "DevSparseMatrix" << endl
         << " height = " << height << ", width = " << width << ", nze = " << nze << endl;
    
    // deprecated
    /*
    descr = new cusparseMatDescr_t;
    cusparseCreateMatDescr (descr);

    cusparseSetMatType(*descr, CUSPARSE_MATRIX_TYPE_GENERAL);
    cusparseSetMatIndexBase(*descr, CUSPARSE_INDEX_BASE_ZERO);
    */

    /* cout << "create device sparse matrix, n = " << height << ", nze = " << nze << endl; */
    
    Array<int> temp_ind (height+1); 
    for (int i = 0; i <= height; i++) temp_ind[i] = mat.First(i); // conversion to 32-bit integer

    cudaMalloc ((void**)&dev_ind, (mat.Height()+1) * sizeof(int));
    cudaMalloc ((void**)&dev_col, (mat.NZE()) * sizeof(int));
    cudaMalloc ((void**)&dev_val, (mat.NZE()) * sizeof(double));
    
    cudaMemcpy (dev_ind, &temp_ind[0], (mat.Height()+1)*sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy (dev_col, &mat.GetRowIndices(0)[0], mat.NZE()*sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy (dev_val, &mat.GetRowValues(0)[0], mat.NZE()*sizeof(double), cudaMemcpyHostToDevice);

    cusparseCreateCsr(&descr, height, width, nze,
                      dev_ind, dev_col, dev_val,
                      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO,
                      CUDA_R_64F);
  }


  DevSparseMatrix :: ~DevSparseMatrix ()
  {
    cusparseDestroySpMat(descr);
    cudaFree(dev_ind);
    cudaFree(dev_col);
    cudaFree(dev_val);
  }
  
  void DevSparseMatrix :: Mult (const BaseVector & x, BaseVector & y) const
  {
    static Timer tmv("CUDA Matrix-Vector Multiplication");
    RegionTimer reg(tmv);

    /* cout << "device mult sparse" << endl; */
    /* cout << "vec0: " << typeid(x).name() << endl; */
    /* cout << "vec1: " << typeid(y).name() << endl; */
    const UnifiedVector & ux = dynamic_cast<const UnifiedVector&> (x);
    UnifiedVector & uy = dynamic_cast<UnifiedVector&> (y);

    ux.UpdateDevice();
    uy = 0.0;
    uy.UpdateDevice();

    double alpha= 1;
    double beta = 0;

    // deprecated
    /*
    cusparseDcsrmv (Get_CuSparse_Handle(), 
                    CUSPARSE_OPERATION_NON_TRANSPOSE, height, width, nze, 
        &alpha, *descr, 
        dev_val, dev_ind, dev_col, 
        ux.dev_data, &beta, uy.dev_data);
    */

    size_t bufferSize = 0;
    void* dBuffer = NULL;

    cusparseSpMV_bufferSize(Get_CuSparse_Handle(), CUSPARSE_OPERATION_NON_TRANSPOSE,
                            &alpha, descr, ux.descr, &beta, uy.descr, CUDA_R_64F,
                            CUSPARSE_MV_ALG_DEFAULT, &bufferSize);
    cudaMalloc(&dBuffer, bufferSize);

    cusparseStatus_t status;
    cusparseSpMV(Get_CuSparse_Handle(), 
                 CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, descr,
                 ux.descr, &beta, uy.descr, CUDA_R_64F,
                 CUSPARSE_SPMV_ALG_DEFAULT, dBuffer);

    /* uy.UpdateHost(); */
    
    uy.host_uptodate = false;
  }


  void DevSparseMatrix :: MultAdd (double s, const BaseVector & x, BaseVector & y) const
  {
    static Timer tmv("CUDA MultAdd");
    RegionTimer reg(tmv);

    const UnifiedVector & ux = dynamic_cast<const UnifiedVector&> (x);
    UnifiedVector & uy = dynamic_cast<UnifiedVector&> (y);

    ux.UpdateDevice();
    uy.UpdateDevice();

    double alpha= 1;
    /* double beta = 1; */
    double beta = s;

    // deprecated
    /*
    cusparseDcsrmv (Get_CuSparse_Handle(), 
                    CUSPARSE_OPERATION_NON_TRANSPOSE, height, width, nze, 
        &alpha, *descr, 
        dev_val, dev_ind, dev_col, 
        ux.dev_data, &beta, uy.dev_data);
    */

    cusparseSpMatDescr_t matA;
    size_t bufferSize = 0;
    void* dBuffer = NULL;

    cusparseSpMV_bufferSize(Get_CuSparse_Handle(), CUSPARSE_OPERATION_NON_TRANSPOSE,
                            &alpha, matA, ux.descr, &beta, uy.descr, CUDA_R_64F,
                            CUSPARSE_MV_ALG_DEFAULT, &bufferSize);
    cudaMalloc(&dBuffer, bufferSize);

    cusparseSpMV(Get_CuSparse_Handle(), 
                 CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA,
                 ux.descr, &beta, uy.descr, CUDA_R_64F,
                 CUSPARSE_MV_ALG_DEFAULT, &bufferSize);

    uy.host_uptodate = false;
  }

  shared_ptr<DevSparseMatrix> MatMult (const DevSparseMatrix& mata, const DevSparseMatrix& matb)
  {
    throw Exception ("DevSparseMatrix MatMult not implemented yet.");
  }

  


  /******************** DevDMatrix ********************/

  DevDMatrix :: DevDMatrix ()
  { }

  DevDMatrix :: DevDMatrix (size_t height, size_t width)
  {
    this->height = height;
    this->width = width;

    cudaMalloc((void**) &dev_data, height * width * sizeof(double));

    /* cusparseCreateDnMat(&descr, height, width, height, dev_data, CUDA_R_64F, CUSPARSE_ORDER_ROW); */
  }

  DevDMatrix :: DevDMatrix (const Matrix<>& mat)
  {
    height = mat.Height();
    width = mat.Width();

    cudaMalloc((void**) &dev_data, height * width * sizeof(double));

    double* host_tmp = mat.Data();
    cudaMemcpy(dev_data, host_tmp, height * width * sizeof(double), cudaMemcpyHostToDevice);

    // in case we need cusparse operations, we could add an cusparse descriptor
    /* cusparseCreateDnMat (&descr, mat.Height(), mat.Width(), mat.Height(), */
    /*                      dev_data, CUDA_R_64F, CUSPARSE_ORDER_ROW); */
  }

  DevDMatrix :: DevDMatrix (const DevDMatrix& mat)
  {
    height = mat.Height();
    width = mat.Width();

    cudaMalloc((void**) &dev_data, height * width * sizeof(double));

    cudaMemcpy(dev_data, mat.DevData(), height * width * sizeof(double), cudaMemcpyHostToDevice);
  }

  DevDMatrix :: ~DevDMatrix ()
  {
    /* cusparseDestroyDnMat (descr); */
    cudaFree(dev_data);
  }

  const DevDMatrix & DevDMatrix :: operator= (double d) const
  {
    cublasDscal(Get_CuBlas_Handle(), height*width, &d, dev_data, 1);

    return *this;
  }

  const DevDMatrix & DevDMatrix :: operator= (const DevDMatrix & mat) const
  {
    cerr << "operator=(DevDMatrix)" << endl;

    // TODO: in case they don't fit, free and reallocate data?
    if ((height != mat.Height()) || (width != mat.Width()))
      throw Exception("sizes of DevDMatrix do not fit during assign");

    cudaMemcpy(dev_data, mat.DevData(), height * width * sizeof(double), cudaMemcpyDeviceToDevice);

    return *this;
  }


  AutoVector DevDMatrix :: CreateRowVector () const
  {
    return make_unique<UnifiedVector>(width);
  }

  AutoVector DevDMatrix :: CreateColVector () const
  {
    return make_unique<UnifiedVector>(height);
  }

  // TODO: currently in-place. change?
  void DevDMatrix :: Add (const BaseMatrix& b)
  {
    const DevDMatrix* devptr = dynamic_cast<const DevDMatrix*>(&b);
    if (!devptr)
    {
      throw Exception("DevDMatrix::Mult only implemented for DevDMatrices (yet).");
    }

    double alpha = 1;

    cublasAxpyEx (Get_CuBlas_Handle(), height*width, &alpha, CUDA_R_64F,
                  devptr->DevData(), CUDA_R_64F, 1, dev_data, CUDA_R_64F, 1, CUDA_R_64F);

    /* cublasDgeam(Get_CuBlas_Handle(), CUBLAS_OP_N, CUBLAS_OP_N, height, width, */
    /*             &alpha, dev_data, width, &beta, b.DevData(), width, dev_data, width); */
  }

  void DevDMatrix :: Scale (double d)
  {
    cublasScalEx(Get_CuBlas_Handle(), height*width, &d, CUDA_R_64F, 
                 dev_data, CUDA_R_64F, 1, CUDA_R_64F);
  }

  void DevDMatrix :: Mult (const BaseVector & x, BaseVector & y) const
  {
    MultAdd(0, x, y);

    /* const UnifiedVector & ux = dynamic_cast<const UnifiedVector&> (x); */
    /* UnifiedVector & uy = dynamic_cast<UnifiedVector&> (y); */

    /* double alpha = 1; */
    /* double beta = 0; */

    /* cublasDgemv(Get_CuBlas_Handle(), CUBLAS_OP_N, height, width, &alpha */
    /*             dev_data, height, ux.DevData(), 1, &beta, uy.DevData(), 1); */
  }

  void DevDMatrix :: MultAdd (double s, const BaseVector& x, BaseVector& y) const
  {
    const UnifiedVector * ux = dynamic_cast<const UnifiedVector*> (&x);
    UnifiedVector * uy = dynamic_cast<UnifiedVector*> (&y);

    if ((!ux) || (!uy))
    {
      throw Exception("Inputs no UnifiedVector (will be fixed)");
    }

    ux->UpdateDevice();
    uy->UpdateDevice();

    double alpha = 1;
    double beta = s;

    // CUBLAS_OP_T since cublas uses cow-major while matrix is row-major
    cublasStatus_t stat = cublasDgemv(Get_CuBlas_Handle(), CUBLAS_OP_T, width, height, &alpha, 
                dev_data, width, ux->DevData(), 1, &beta, uy->DevData(), 1);

    uy->host_uptodate = false;
  }

  void DevDMatrix :: SetZero ()
  {
    double alpha = 0;
    double beta = 0;

    // special case
    // see cublas documentation
    cublasDgeam (Get_CuBlas_Handle(), CUBLAS_OP_N, CUBLAS_OP_N, height, width,
                 &alpha, nullptr, height, &beta, nullptr, height, dev_data, height);
  }

  double* DevDMatrix :: DevData () const
  {
    return dev_data;
  }

  ostream & DevDMatrix :: Print (ostream & ost) const
  {
    cout << "output dense device Matrix of size " << height << "x" << width << endl;
    Matrix<> tmp (height, width);
    cudaMemcpy(tmp.Data(), dev_data, height * width * sizeof(double), cudaMemcpyDeviceToHost);
    ost << tmp << endl;

    return ost;
  }

  // TODO: fix
  shared_ptr<DevDMatrix> MatMult (const DevDMatrix& mata, const DevDMatrix& matb)
  {
    throw Exception("DevDMatrix MatMult not implemented yet.");

    int m = mata.Height();
    int k = mata.Width();
    int n = matb.Width();

    int lda = k; // op(A) is lda x k (CUBLAS_OP_N) or lda x m (CUBLAS_OP_T)
    int ldb = n; // op(B) is ldb x n (CUBLAS_OP_N) or ldb x k (CUBLAS_OP_T)
    int ldc = m; // C is ldc x n

    double alpha = 1;
    double beta = 0;

    DevDMatrix c(m, n);

    /* cublasDgemm (Get_CuBlas_Handle(), CUBLAS_OP_T, CUBLAS_OP_T, m, n, k, */
    /*              &alpha, mata.DevData(), k, matb.DevData(), n, &beta, c.DevData(), m); */

    cublasDgemm(Get_CuBlas_Handle(), CUBLAS_OP_N, CUBLAS_OP_N, n, m, k, &alpha, matb.DevData(), n, mata.DevData(), k, &beta, c.DevData(), n );

    return make_shared<DevDMatrix>(c);
  }


  /******************** DevEBEMatrix ********************/

  DevEBEMatrix :: DevEBEMatrix (const ConstantElementByElementMatrix& ebemat)
    : devmat(ebemat.GetMatrix()), col_dnums(ebemat.GetColDNums()), row_dnums(ebemat.GetRowDNums())
  { 
    throw Exception("DevEBEMatrix not implemented yet.");


  }

  DevEBEMatrix :: ~DevEBEMatrix ()
  { }

  AutoVector DevEBEMatrix :: CreateRowVector () const
  {
    return make_unique<UnifiedVector>(width);
  }

  AutoVector DevEBEMatrix :: CreateColVector () const
  {
    return make_unique<UnifiedVector>(height);
  }

  void DevEBEMatrix :: MultAdd (double s, const UnifiedVector& x, UnifiedVector& y)
  {
    static Timer timer("Dev-EBE-Matrix::MultAdd");
    RegionTimer reg(timer);

    size_t maxs = 0;
    for (size_t i=0; i<col_dnums.Size(); i++)
      maxs = max2 (maxs, col_dnums[i].Size());

    /* ebe_multadd_kernel(); */

    throw Exception("DevEBEMatrix::MultAdd not implemented yet.");
  }

  /* void DevEBEMatrix :: Scale (double d) */
  /* { */
  /*   throw Exception("DevEBEMatrix::Scale not implemented yet."); */
  /* } */



  /* BaseVector& operator* (const UnifiedVector& v, const DevSparseMatrix& mat) */
  /* { */
  /*   shared_ptr<UnifiedVector> res = make_shared<UnifiedVector>(v.Size()); */
  /*   mat.Mult(v, *res); */
  /*   return *res; */
  /* } */



  /******************** DevJacobiPrecond ********************/

  // use_par is currently not in use. maybe important later
  DevJacobiPrecond :: DevJacobiPrecond (const SparseMatrix<double> & amat, 
    shared_ptr<BitArray> ainner, bool use_par)
      : inner(ainner)
  {

    height = amat.Height();
    width = amat.Height();
    nze = 0;
    Array<int> tmp_ind(height+1);
    Array<int> tmp_col(height);
    Array<double> tmp_val(height);

    tmp_ind[0] = 0;
    for (int i=0; i<height; i++)
    {
      if (!inner || inner->Test(i))
      {
        tmp_col[nze] = i;
        tmp_ind[i+1] = tmp_ind[i]+1;
        tmp_val[nze] = 1 / amat(i,i);
        nze++;
      }
      else
      {
        tmp_ind[i+1] = tmp_ind[i];
      }
    }
    
    cudaMalloc((void**) &dev_ind, (height+1) * sizeof(int));
    cudaMalloc((void**) &dev_col, nze * sizeof(int));
    cudaMalloc((void**) &dev_val, nze * sizeof(double));

    cudaMemcpy(dev_ind, tmp_ind.begin(), (height + 1) * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(dev_col, tmp_col.begin(), nze * sizeof(int), cudaMemcpyHostToDevice);
    cudaMemcpy(dev_val, tmp_val.begin(), nze * sizeof(double), cudaMemcpyHostToDevice);

    cusparseCreateCsr(&descr, height, height, nze,
                      dev_ind, dev_col, dev_val,
                      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO,
                      CUDA_R_64F);
  }

  // TODO: should be like this, but data from host_jac is not accessible
  /* DevJacobiPrecond :: DevJacobiPrecond (const JacobiPrecond<double> & host_jac) */
  /* { */
  /*   height = host_jac.height; */
  /*   inner = host_jac.inner; */

  /*   cudaMalloc((void**) dev_invdiag, height * sizeof(double)); */
  /*   cudaMemcpy(dev_invdiag, host_jac.invdiag.begin(), height * sizeof(double), cudaMemcpyHostToDevice); */
  /* } */

  DevJacobiPrecond :: ~DevJacobiPrecond ()
  {
    /* cerr << "DevJacobiPrecond dtor" << endl; */
    /* cudaFree(dev_invdiag); */
  }

  /* void DevJacobiPrecond :: Mult (const BaseVector & x, BaseVector & y) const */
  /* { */
  /*   MultAdd(0, x, y); */
  /* } */


  // will be used instead of creating a DevSparseMatrix
  /* void DevJacobiPrecond :: MultAdd (double d, const BaseVector & x, BaseVector & y) const */
  /* { */
    
  /*   const UnifiedVector * ux = dynamic_cast<const UnifiedVector*> (&x); */
  /*   UnifiedVector * uy = dynamic_cast<UnifiedVector*> (&y); */

  /*   if ((!ux) || (!uy)) */
  /*   { */
  /*     throw Exception("MultAdd only available for UnifiedVector"); */
  /*   } */

  /*   ux->UpdateDevice(); */
  /*   uy->UpdateDevice(); */

  /*   double alpha = 1; */
  /*   double beta = d; */

  /*   // using banded band width 0 */
  /*   // TODO: try own kernel, since cublas does not provide element-wise vector-vector */ 
  /*   //          resp. diag multadd */
  /*   cublasDgbmv(Get_CuBlas_Handle(), CUBLAS_OP_N, Height(), Height(), 0, 0, &alpha, */
  /*               dev_invdiag, Height(), ux->DevData(), 1, &beta, uy->DevData(), 1); */

  /*   uy->host_uptodate = false; */
  /* } */


  /* DevJacobiPreconditioner :: DevJacobiPreconditioner (const SparseMatrix<double> & mat, */
  /*                 const BitArray & freedofs) */
  /* { */
  /*   height = mat.Height(); */
  /*   width = mat.Height(); */
  /*   nze = mat.Height(); */

  /*   descr = new cusparseMatDescr_t; */
  /*   cusparseCreateMatDescr (descr); */

  /*   cusparseSetMatType(*descr, CUSPARSE_MATRIX_TYPE_GENERAL); */
  /*   cusparseSetMatIndexBase(*descr, CUSPARSE_INDEX_BASE_ZERO); */

  /*   cout << "create Jacobi preconditioner" << endl; */
    
  /*   Array<int> temp_ind (height+1); */ 
  /*   Array<int> temp_cols (height); */
  /*   Array<double> temp_vals (height); */

  /*   for (int i = 0; i <= height; i++) temp_ind[i] = i; */ 

  /*   for (int i = 0; i < height; i++) */
  /*     { */
  /*       temp_cols[i] = i; */
  /*       if (freedofs.Test(i)) */
  /*         temp_vals[i] = 1.0 / mat(i,i); */
  /*       else */
  /*         temp_vals[i] = 0.0; */
  /*     } */

  /*   cudaMalloc ((void**)&dev_ind, (height+1) * sizeof(int)); */
  /*   cudaMalloc ((void**)&dev_col, (height) * sizeof(int)); */
  /*   cudaMalloc ((void**)&dev_val, (height) * sizeof(double)); */

  /*   cudaMemcpy (dev_ind, &temp_ind[0], (height+1)*sizeof(int), cudaMemcpyHostToDevice); */
  /*   cudaMemcpy (dev_col, &temp_cols[0], height*sizeof(int), cudaMemcpyHostToDevice); */
  /*   cudaMemcpy (dev_val, &temp_vals[0], height*sizeof(double), cudaMemcpyHostToDevice); */
  /* } */
  
  /* void DevJacobiPreconditioner :: Mult (const BaseVector & x, BaseVector & y) const */
  /* { */
  /*   // cout << "device mult precond" << endl; */

  /*   const UnifiedVector & ux = dynamic_cast<UnifiedVector*> (x); */
  /*   UnifiedVector & uy = dynamic_cast<UnifiedVector*> (y); */

  /*   ux.UpdateDevice(); */
  /*   uy = 0.0; */
  /*   uy.UpdateDevice(); */

  /*   double alpha= 1; */
  /*   double beta = 0; */

    /* // TODO: fix this */
  /*   /1* cusparseDcsrmv (Get_CuSparse_Handle (), *1/ */ 
  /*   /1*                 CUSPARSE_OPERATION_NON_TRANSPOSE, height, width, nze, *1/ */ 
        /* /1* &alpha, *descr, *1/ */ 
        /* /1* dev_val, dev_ind, dev_col, *1/ */ 
        /* /1* ux.dev_data, &beta, uy.dev_data); *1/ */

  /*   uy.host_uptodate = false; */

  /*   // cout << "mult complete" << endl; */
  /* } */


  /* void DevJacobiPreconditioner :: MultAdd (double s, const BaseVector & x, BaseVector & y) const */
  /* { */
  /*   cout << "device multadd precond" << endl; */

  /*   const UnifiedVector & ux = dynamic_cast<UnifiedVector*>(x); */
  /*   UnifiedVector & uy = dynamic_cast<UnifiedVector*>(y); */

  /*   ux.UpdateDevice(); */
  /*   uy.UpdateDevice(); */

  /*   double alpha= 1; */
  /*   double beta = 1; */


    /* // TODO: fix this */
  /*   /1* cusparseDcsrmv (Get_CuSparse_Handle (), *1/ */ 
  /*   /1*                 CUSPARSE_OPERATION_NON_TRANSPOSE, height, width, nze, *1/ */ 
        /* /1* &alpha, *descr, *1/ */ 
        /* /1* dev_val, dev_ind, dev_col, *1/ */ 
        /* /1* ux.dev_data, &beta, uy.dev_data); *1/ */

  /*   uy.host_uptodate = false; */
  /*   cout << "mult complete" << endl; */
  /* } */

}
