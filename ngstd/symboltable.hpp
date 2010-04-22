#ifndef FILE_NGS_SYMBOLTABLE
#define FILE_NGS_SYMBOLTABLE

/**************************************************************************/
/* File:   symboltable.hpp                                                */
/* Author: Joachim Schoeberl                                              */
/* Date:   01. Jun. 95                                                    */
/**************************************************************************/

namespace ngstd
{

/**
  Base class for generic SymbolTable.
  Maintains the array of identifiers.
 */
class BaseSymbolTable
{
protected:
  /// identifiers
  Array <char*> names;
  
public:
  /// 
  NGS_DLL_HEADER BaseSymbolTable ();
  /// deletes identifiers
  NGS_DLL_HEADER ~BaseSymbolTable ();
  /// delete all symbols
  NGS_DLL_HEADER void DelNames ();

  /// append new name (copy)
  NGS_DLL_HEADER void AppendName (const char * name);

  /// Index of symbol name, throws exception if unsued
  NGS_DLL_HEADER int Index (const char * name) const;

  /// Index of symbol name, returns -1 if unused
  NGS_DLL_HEADER int CheckIndex (const char * name) const;
};






/** 
    A symbol table.
   
    The symboltable provides a mapping from string identifiers
    to the generic type T. The strings are copied.
*/
template <class T>
class SymbolTable : public BaseSymbolTable
{
  /// the data
  Array <T> data;
public:
  /// Creates a symboltable
  SymbolTable ()
  { ; }

  /// number of identifiers
  int Size() const
  { 
    return data.Size(); 
  }


  /// Returns reference to element. exception for unused identifier
  T & operator[] (const char * name)
  {
    return data[Index (name)]; 
  }

  /// Returns reference to element. exception for unused identifier
  T & operator[] (const string & name)
  {
    return data[Index (name.c_str())]; 
  }

  /// Returns element, error if not used
  const T & operator[] (const char * name) const
  {
    return data[Index (name)]; 
  }

  const T & operator[] (const string & name) const
  {
    return data[Index (name.c_str())]; 
  }

  /// Returns reference to i-th element
  T & operator[] (int i)
  { return data[i]; } 

  /// Returns const reference to i-th element
  const T & operator[] (int i) const
  { return data[i]; } 

  /// Returns name of i-th element
  const char* GetName (int i) const
  { return names[i]; }


  /// Associates el to the string name, overrides if name is used
  void Set (const char * name, const T & el)
  {
    int i = CheckIndex (name);
    if (i >= 0) 
      data[i] = el;
    else
      {
	data.Append (el);
	AppendName (name);
      }
  }

  void Set (const string & name, const T & el)
  {
    Set (name.c_str(), el);
  }

  /// Checks whether name is used
  bool Used (const char * name) const
  {
    return (CheckIndex(name) >= 0) ? 1 : 0;
  }

  bool Used (const string & name) const
  {
    return Used (name.c_str());
  }

  /// Deletes symboltable
  inline void DeleteAll ()
  {
    DelNames();
    data.DeleteAll();
  }


  SymbolTable<T> & operator= (const SymbolTable<T> & tab2)
  {
    for (int i = 0; i < tab2.Size(); i++)
      Set (tab2.GetName(i), tab2[i]);
    return *this;
  }
};

template <typename T>
ostream & operator<< (ostream & ost, SymbolTable<T> & st)
{
  for (int i = 0; i < st.Size(); i++)
    ost << st.GetName(i) << " : " << st[i] << endl;
  return ost;
}

}

#endif
