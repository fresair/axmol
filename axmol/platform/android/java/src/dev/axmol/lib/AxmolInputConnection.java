/****************************************************************************
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/
package dev.axmol.lib;

import android.content.Context;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.Selection;
import android.text.Spanned;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.InputMethodManager;

/** The actual Android Editable and the attached native InputDelegate share an
 * editor generation. Modern delegates receive separate preedit/commit events;
 * the explicitly opted-in legacy TextField receives the Editable's native delta.
 */
public class AxmolInputConnection extends BaseInputConnection {
    private static final int COMPOSE = 0, COMMIT = 1, DELETE = 2, SELECT = 3, FINISH = 4;
    private final View mTargetView;
    private final AxmolPlayer.IMEState mState;
    private volatile boolean mClosed;
    private long mSequence;

    public AxmolInputConnection(View targetView, boolean fullEditor) {
        this(targetView, AxmolPlayer.currentIMEState());
    }

    AxmolInputConnection(View targetView, AxmolPlayer.IMEState state) {
        // Fallback/dummy mode synthesizes key events and clears committed text.
        // This is a real editor, so BaseInputConnection must keep its Editable.
        super(targetView, true);
        mTargetView = targetView;
        mState = state;
        Editable content = getEditable();
        content.replace(0, content.length(), state.text);
        Selection.setSelection(content, toUtf16(content, state.selectionStart), toUtf16(content, state.selectionEnd));
        if (state.charLimit > 0) content.setFilters(new InputFilter[]{new CodePointLimit(state.charLimit)});
    }

    private static int toUtf16(CharSequence text, int codePoints) {
        String value = text.toString();
        return value.offsetByCodePoints(0, Math.max(0, Math.min(codePoints, value.codePointCount(0, value.length()))));
    }

    private static int codePointOffset(CharSequence text, int utf16) {
        return Character.codePointCount(text, 0, Math.max(0, Math.min(utf16, text.length())));
    }

    private static int boundary(CharSequence text, int offset) {
        offset = Math.max(0, Math.min(offset, text.length()));
        if (offset > 0 && offset < text.length() && Character.isLowSurrogate(text.charAt(offset)) &&
                Character.isHighSurrogate(text.charAt(offset - 1))) --offset;
        return offset;
    }

    private static final class CodePointLimit implements InputFilter {
        private final int maximum;
        CodePointLimit(int maximum) { this.maximum = maximum; }
        @Override public CharSequence filter(CharSequence source, int start, int end, Spanned dest, int dstart, int dend) {
            int available = maximum - Character.codePointCount(dest, 0, dest.length()) +
                    Character.codePointCount(dest, dstart, dend);
            if (available <= 0) return "";
            if (Character.codePointCount(source, start, end) <= available) return null;
            int cut = Character.offsetByCodePoints(source, start, available);
            return source.subSequence(start, Math.min(cut, end));
        }
    }

    private void selectionChanged() {
        Editable content = getEditable();
        InputMethodManager imm = (InputMethodManager)mTargetView.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null && !mClosed) imm.updateSelection(mTargetView, Selection.getSelectionStart(content),
                Selection.getSelectionEnd(content), getComposingSpanStart(content), getComposingSpanEnd(content));
    }

    private void dispatch(int operation, String payload, int deleteBefore) {
        if (mClosed || mState.generation == 0) return;
        Editable content = getEditable();
        final String document = content.toString();
        final int start = codePointOffset(content, Selection.getSelectionStart(content));
        final int end = codePointOffset(content, Selection.getSelectionEnd(content));
        int composingStart = getComposingSpanStart(content);
        final int caret = composingStart >= 0 ? Math.min(payload.codePointCount(0, payload.length()),
                Math.max(0, end - codePointOffset(content, composingStart))) : 0;
        final long sequence = ++mSequence;
        final String eventText = payload;
        final int op = operation;
        AxmolEngine.runOnAxmolThread(() -> {
            // closeConnection and native detach/VM stop independently invalidate
            // this operation; an old connection can never edit a new delegate.
            if (mClosed) return;
            final String[] actual = AxmolPlayer.nativeUpdateIME(mState.generation, op, document,
                    start, end, eventText, deleteBefore, caret);
            if (actual != null)
                mTargetView.post(() -> acknowledge(sequence, op, actual));
        });
        selectionChanged();
    }

    private String composingText() {
        Editable content = getEditable();
        int start = getComposingSpanStart(content), end = getComposingSpanEnd(content);
        return start >= 0 && end >= start ? content.subSequence(start, end).toString() : "";
    }

    private void acknowledge(long sequence, int operation, String[] actual) {
        if (mClosed || sequence != mSequence || actual.length != 3) return;
        AxmolPlayer.recordIMEState(this, mState.generation, actual);
        if (!mState.legacyEditing) return;
        Editable content = getEditable();
        int composingStart = getComposingSpanStart(content), composingEnd = getComposingSpanEnd(content);
        int composingStartCP = composingStart < 0 ? -1 : codePointOffset(content, composingStart);
        int composingEndCP = composingEnd < 0 ? -1 : codePointOffset(content, composingEnd);
        if (!content.toString().equals(actual[0])) {
            removeComposingSpans(content);
            content.replace(0, content.length(), actual[0]);
            if (operation == COMPOSE && composingStartCP >= 0)
                super.setComposingRegion(toUtf16(content, composingStartCP), toUtf16(content, composingEndCP));
        }
        Selection.setSelection(content, toUtf16(content, Integer.parseInt(actual[1])),
                toUtf16(content, Integer.parseInt(actual[2])));
        selectionChanged();
    }

    @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
        if (mClosed || text == null) return false;
        boolean accepted = super.setComposingText(text, newCursorPosition);
        if (accepted) dispatch(COMPOSE, composingText(), 0);
        return accepted;
    }

    @Override public boolean setComposingRegion(int start, int end) {
        if (mClosed) return false;
        boolean accepted = super.setComposingRegion(boundary(getEditable(), start), boundary(getEditable(), end));
        if (accepted) dispatch(COMPOSE, composingText(), 0);
        return accepted;
    }

    @Override public boolean commitText(CharSequence text, int newCursorPosition) {
        if (mClosed || text == null) return false;
        boolean accepted = super.commitText(text, newCursorPosition);
        if (accepted) dispatch(COMMIT, text.toString(), 0);
        return accepted;
    }

    @Override public boolean finishComposingText() {
        boolean accepted = super.finishComposingText();
        if (accepted && !mClosed) dispatch(FINISH, "", 0);
        return accepted;
    }

    @Override public boolean setSelection(int start, int end) {
        if (mClosed) return false;
        boolean accepted = super.setSelection(boundary(getEditable(), start), boundary(getEditable(), end));
        if (accepted) dispatch(SELECT, "", 0);
        return accepted;
    }

    @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        if (mClosed || beforeLength < 0 || afterLength < 0) return false;
        Editable content = getEditable();
        int a = Math.min(Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
        int b = Math.max(Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
        int ca = getComposingSpanStart(content), cb = getComposingSpanEnd(content);
        if (ca >= 0 && cb >= 0) { a = Math.min(a, ca); b = Math.max(b, cb); }
        if (a < 0 || b < 0) return false;
        int start = boundary(content, Math.max(0, a - beforeLength));
        int finish = (int)Math.min(content.length(), (long)b + afterLength);
        if (finish > 0 && finish < content.length() && Character.isLowSurrogate(content.charAt(finish)) &&
                Character.isHighSurrogate(content.charAt(finish - 1))) ++finish;
        int deletedCP = Character.codePointCount(content, start, a);
        boolean accepted = super.deleteSurroundingText(a - start, finish - b);
        if (accepted) dispatch(DELETE, "", deletedCP);
        return accepted;
    }

    @Override public boolean deleteSurroundingTextInCodePoints(int beforeLength, int afterLength) {
        if (mClosed || beforeLength < 0 || afterLength < 0) return false;
        Editable content = getEditable();
        int a = Math.min(Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
        int b = Math.max(Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
        int ca = getComposingSpanStart(content), cb = getComposingSpanEnd(content);
        if (ca >= 0 && cb >= 0) { a = Math.min(a, ca); b = Math.max(b, cb); }
        if (a < 0 || b < 0) return false;
        int start = Character.offsetByCodePoints(content, a, -Math.min(beforeLength, Character.codePointCount(content, 0, a)));
        int end = Character.offsetByCodePoints(content, b, Math.min(afterLength, Character.codePointCount(content, b, content.length())));
        // Uses the API23 base operation too, without leaving half a surrogate.
        return deleteSurroundingText(a - start, end - b);
    }

    @Override public boolean sendKeyEvent(KeyEvent event) {
        if (mClosed) return false;
        if (event.getKeyCode() == KeyEvent.KEYCODE_DEL || event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
            if (event.getAction() == KeyEvent.ACTION_UP) return true;
            if (event.getAction() == KeyEvent.ACTION_DOWN)
                return event.getKeyCode() == KeyEvent.KEYCODE_DEL ? deleteSurroundingTextInCodePoints(1, 0) : commitText("\n", 1);
        }
        return super.sendKeyEvent(event);
    }

    @Override public void closeConnection() {
        mClosed = true;
        ++mSequence;
        // InputConnection.closeConnection was added in API24; our engine also
        // closes explicitly when switching editors on the supported API23.
        if (Build.VERSION.SDK_INT >= 24) super.closeConnection();
        else super.finishComposingText();
    }
}
