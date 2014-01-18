// LeftView.h : interface of the CLeftView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_LEFTVIEW_H__BD78922C_6B44_4149_8FF4_5EE4C83D09A0__INCLUDED_)
#define AFX_LEFTVIEW_H__BD78922C_6B44_4149_8FF4_5EE4C83D09A0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Afxtempl.h"
#include "irodsWinCollection.h"

class CInquisitorDoc;

class CLeftView : public CTreeView
{
protected: // create from serialization only
	CLeftView();
	DECLARE_DYNCREATE(CLeftView)

// Attributes
public:

	void OnDragLeave();
	DROPEFFECT OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);
	DROPEFFECT OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point);


	CInquisitorDoc* GetDocument();
	COleDropTarget m_dropTarget;

//	LRESULT OnRefresh(WPARAM wParam, LPARAM lParam);
	//void InformCopy(WINI::StatusCode* status, WINI::INode* result);
	void OnBack();
	void OnForward();
//	WINI::StatusCode AddMetadataAttribute(const char* attribute);
//	WINI::StatusCode SetMetadataValue(const char* attribute, const char* value);
	LRESULT OnUpdateCopy(WPARAM wParam, LPARAM lParam);

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CLeftView)
	public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual BOOL OnDrop(COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point);
	protected:
	virtual void OnInitialUpdate(); // called first time after construct
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CLeftView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif
    //LRESULT ConnectedUpdate(WPARAM wParam, LPARAM lParam);
protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CLeftView)
	afx_msg void OnSelchanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnDelete();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDownload();
	afx_msg void OnAccessCtrl();
	//afx_msg void OnUpload();
	afx_msg void OnUploadFolder();
	afx_msg void OnNewCollection();
	afx_msg void OnUpdateViewLargeicon(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewList(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewSmallicon(CCmdUI* pCmdUI);
	afx_msg void OnUpdateViewDetails(CCmdUI* pCmdUI);
	//afx_msg void OnUpdateUpload(CCmdUI* pCmdUI);
	//afx_msg void OnUpdateUploadFolder(CCmdUI* pCmdUI);
	//afx_msg void OnUpdateDownload(CCmdUI* pCmdUI);
	afx_msg void OnUpdateNewCollection(CCmdUI* pCmdUI);
	afx_msg void OnUpdateAccessCtrl(CCmdUI* pCmdUI);
	afx_msg void OnNewContainer();
	afx_msg void OnReplicate();
	afx_msg void OnMetadata();
	//afx_msg void OnUpdateMetadata(CCmdUI* pCmdUI);
	afx_msg void OnUpdateSynchronize(CCmdUI* pCmdUI);
	afx_msg void OnUpdateNewContainer(CCmdUI* pCmdUI);
	//afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnEditCopy();
	afx_msg void OnUpdateEditCopy(CCmdUI* pCmdUI);
	//afx_msg void OnEditCut();
	//afx_msg void OnUpdateEditCut(CCmdUI* pCmdUI);
	afx_msg void OnEditPaste();
	afx_msg void OnUpdateEditPaste(CCmdUI* pCmdUI);
	afx_msg void OnBegindrag(NMHDR* pNMHDR, LRESULT* pResult);
	//afx_msg void OnUpdateDelete(CCmdUI* pCmdUI);
	afx_msg void OnRename();
	afx_msg void OnUpdateRename(CCmdUI* pCmdUI);
	//afx_msg void OnComment();
	//afx_msg void OnUpdateComment(CCmdUI* pCmdUI);
	//afx_msg void OnBeginlabeledit(NMHDR* pNMHDR, LRESULT* pResult);
	//afx_msg void OnEndlabeledit(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNewMeta();
	//afx_msg void OnUpdateNewMeta(CCmdUI* pCmdUI);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
//CHARLIECHARLIE
private:
	HTREEITEM m_collection_root;
	HTREEITEM m_query_root;
	HTREEITEM m_resource_root;

	HTREEITEM selected_node;
	HTREEITEM m_drag_over_item_node;
    /*
	bool m_bDontChange;
	bool m_bPossibleDrag;

	CMap<WINI::INode*, WINI::INode*, HTREEITEM, HTREEITEM&> m_ReverseMap;
	CMap<HTREEITEM, HTREEITEM&, HTREEITEM, HTREEITEM&> m_CZoneToRZoneMap;
	CMap<HTREEITEM, HTREEITEM&, HTREEITEM, HTREEITEM&> m_RZoneToCZoneMap;
    */

	void delete_one_node(HTREEITEM hDelNode);
	void delete_child_nodes(HTREEITEM hNode);
	void dragdrop_de_higlite();

public:
	void clear_tree();
	void delete_node_from_selected_parent(CString & nodeFullPath);
	void delete_dataobj_from_selected_parent(CString & dataName, CString & parCollPath, int replNum);
	void change_one_collname_from_selected_parent(CString & collOldFullPath, CString & newName);
	void change_one_dataname_from_selected_parent(CString & oldName, CString newName);
	void refresh_children_for_a_tree_node(HTREEITEM theNode);
	void refresh_disp_with_a_node_in_tree(HTREEITEM theNode);
	void add_child_collections(HTREEITEM parNode, CArray<irodsWinCollection, irodsWinCollection> & colls);
	void rlist_dbclick_coll_action(CString & collFullPath);
	void tree_goto_parent_node();
	void SetSelectedNodeChildQueryBit(CString & childFullPath, bool newval);
public:
	afx_msg void OnUploadUploadafolder();
	afx_msg void OnUploadUploadfiles();
	afx_msg void OnUpdateSelectAll(CCmdUI *pCmdUI);
	afx_msg void OnEditCopypathtoclipboard();
	afx_msg void OnViewRefresh();
	afx_msg void OnNMRclick(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnEditMetadata();
	afx_msg void OnUpdateEditMetadataSystemmetadata(CCmdUI *pCmdUI);
	afx_msg void OnEditMetadataUsermetadata();
};

#ifndef _DEBUG  // debug version in LeftView.cpp
inline CInquisitorDoc* CLeftView::GetDocument()
   { return (CInquisitorDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_LEFTVIEW_H__BD78922C_6B44_4149_8FF4_5EE4C83D09A0__INCLUDED_)
