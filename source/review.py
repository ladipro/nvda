#A part of NonVisual Desktop Access (NVDA)
#This file is covered by the GNU General Public License.
#See the file COPYING for more details.
#Copyright (C) 2013 Michael Curran <mick@nvaccess.org>

from collections import OrderedDict
import api
import winUser
from NVDAObjects.window import Window
from displayModel import DisplayModelTextInfo
import textInfos

def getObjectPosition(obj):
	"""
	Fetches a TextInfo instance suitable for reviewing the text in  the given object.
	@param obj: the NVDAObject to review
	@type obj: L{NVDAObject}
	@return: the TextInfo instance and the Scriptable object the TextInfo instance is referencing, or None on error. 
	@rtype: (L{TextInfo},L{ScriptableObject})
	"""
	try:
		pos=obj.makeTextInfo(textInfos.POSITION_CARET)
	except (NotImplementedError, RuntimeError):
		pos=obj.makeTextInfo(textInfos.POSITION_FIRST)
	return pos,pos.obj

def getDocumentPosition(obj):
	"""
	Fetches a TextInfo instance suitable for reviewing the text in  the given object's L{TreeInterceptor}, positioned at the object.
	@param obj: the NVDAObject to review
	@type obj: L{NVDAObject}
	@return: the TextInfo instance and the Scriptable object the TextInfo instance is referencing, or None on error. 
	@rtype: (L{TextInfo},L{ScriptableObject})
	"""
	if not obj.treeInterceptor: return None
	try:
		pos=obj.treeInterceptor.makeTextInfo(obj)
	except LookupError:
		return None
	return pos,pos.obj

def getScreenPosition(obj):
	"""
	Fetches a TextInfo instance suitable for reviewing the screen, positioned at the given object's coordinates. 
	@param obj: the NVDAObject to review
	@type obj: L{NVDAObject}
	@return: the TextInfo instance and the Scriptable object the TextInfo instance is referencing, or None on error. 
	@rtype: (L{TextInfo},L{ScriptableObject})
	"""
	focus=api.getFocusObject()
	while focus and not isinstance(focus,Window):
		focus=focus.parent
	if not focus: return None
	w=winUser.getAncestor(focus.windowHandle,winUser.GA_ROOT) or focus.windowHandle
	s=Window(windowHandle=w)
	if s:
		s.redraw()
		try:
			pos=DisplayModelTextInfo(s,obj)
		except LookupError:
			pos=DisplayModelTextInfo(s,textInfos.POSITION_FIRST)
		return pos,pos.obj

modes=[
	('object',_("Object review"),getObjectPosition),
	('document',_("Document review"),getDocumentPosition),
	('screen',_("Screen review"),getScreenPosition),
]

_currentMode=0

def getPositionForCurrentMode(obj):
	"""
	Fetches a TextInfo instance suitable for reviewing the text in or around the given object, according to the current review mode. 
	@param obj: the NVDAObject to review
	@type obj: L{NVDAObject}
	@return: the TextInfo instance and the Scriptable object the TextInfo instance is referencing, or None on error. 
	@rtype: (L{TextInfo},L{ScriptableObject})
	"""
	mode=_currentMode
	while mode>=0:
		pos=modes[mode][2](obj)
		if pos:
			return pos
		mode-=1

def setCurrentMode(mode):
	"""
	Sets the current review mode to the given mode ID or index and updates the review position.
	@param mode: either a 0-based index into the modes list, or one of the mode IDs (first item of a tuple in the modes list).
	@type mode: int or string
	@return: a presentable label for the new current mode (suitable for speaking or brailleing)
	@rtype: string
	"""
	global _currentMode
	if isinstance(mode,int):
		ID,label,func=modes[mode]
	else:
		for index,(ID,label,func) in enumerate(modes):
			if mode==label:
				mode=index
				break
		else:
			raise LookupError("mode %s not found"%mode)
	obj=api.getNavigatorObject()
	pos=func(obj)
	if pos:
		_currentMode=mode
		api.setReviewPosition(pos[0],clearNavigatorObject=False)
		return label

def nextMode(prev=False,startMode=None):
	"""
	Sets the current review mode to the next available  mode and updates the review position. 
	@param prev: if true then switch to the previous mode. If false, switch to the next mode.
	@type prev: bool
	@return: a presentable label for the new current mode (suitable for speaking or brailleing)
	@rtype: string
	"""
	if startMode is None:
		startMode=_currentMode
	newMode=startMode+(1 if not prev else -1)
	if newMode<0 or newMode>=len(modes):
		return None
	label=setCurrentMode(newMode)
	return label or nextMode(prev=prev,startMode=newMode)
