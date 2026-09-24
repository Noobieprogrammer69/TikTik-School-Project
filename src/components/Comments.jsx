import { useState } from 'react'
import { AiFillHeart, AiOutlineHeart } from 'react-icons/ai'
import { BsPinAngle, BsPinAngleFill, BsReply } from 'react-icons/bs'
import { GoVerified } from 'react-icons/go'
import { Link } from 'react-router-dom'

import useAuthStore from '../store/authStore'
import Avatar from './Avatar'
import NoResults from './NoResults'
import RichText from './RichText'

const Comments = ({
  comment,
  setComment,
  addComment,
  comments = [],
  pending,
  postOwnerId,
  onLikeComment,
  onReply,
  onPin,
  onEditComment,
  onDeleteComment,
  onEditReply,
  onDeleteReply,
}) => {
  const { userProfile, allUsers } = useAuthStore()
  const [replyingTo, setReplyingTo] = useState('')
  const [replyText, setReplyText] = useState('')
  const [editing, setEditing] = useState('')
  const [editText, setEditText] = useState('')
  const usersById = new Map(allUsers.map((user) => [user._id, user]))
  const orderedComments = [...comments].sort((left, right) => Number(right.pinned) - Number(left.pinned))

  const userFor = (reference) => {
    const id = reference?._id || reference?._ref
    return usersById.get(id) || { _id: id, userName: 'Unknown User', image: '' }
  }

  const submitReply = async (event, commentId) => {
    event.preventDefault()
    const value = replyText.trim()
    if (!value) return
    await onReply(commentId, value)
    setReplyText('')
    setReplyingTo('')
  }

  return (
    <section className="comments-panel">
      <div className="comments-list" aria-label="Comments">
        {orderedComments.length ? (
          orderedComments.map((item) => {
            const user = userFor(item.postedBy)
            const liked = Boolean(userProfile && item.likes?.some((like) => like._ref === userProfile._id))
            return (
              <article className={`comment-item ${item.pinned ? 'comment-pinned' : ''}`} key={item._key}>
                {item.pinned && <p className="pinned-label"><BsPinAngleFill /> Pinned by creator</p>}
                <div className="comment-main">
                  <Link to={`/profile/${encodeURIComponent(user._id)}`}>
                    <Avatar src={user.image} className="comment-avatar" alt="" />
                  </Link>
                  <div className="comment-body">
                    <Link to={`/profile/${encodeURIComponent(user._id)}`} className="comment-author">
                      {user.userName} <GoVerified />
                    </Link>
                    <p><RichText text={item.comment} /></p>
                    {item.editedAt && <small className="edited-label">Edited</small>}
                    <div className="comment-actions">
                      {userProfile && (
                        <>
                          <button
                            type="button"
                            aria-label={liked ? 'Unlike comment' : 'Like comment'}
                            aria-pressed={liked}
                            disabled={pending === `comment-like-${item._key}`}
                            onClick={() => onLikeComment(item._key, !liked)}
                          >
                            {liked ? <AiFillHeart className="text-pink-500" /> : <AiOutlineHeart />}
                            {item.likes?.length || 0}
                          </button>
                          <button type="button" onClick={() => setReplyingTo(replyingTo === item._key ? '' : item._key)}>
                            <BsReply /> Reply
                          </button>
                          {userProfile._id === user._id && (
                            <button type="button" onClick={() => { setEditing(`comment:${item._key}`); setEditText(item.comment) }}>Edit</button>
                          )}
                          {(userProfile._id === user._id || userProfile._id === postOwnerId) && (
                            <button type="button" className="danger-text" onClick={() => {
                              if (window.confirm('Remove this comment?')) onDeleteComment(item._key)
                            }}>{userProfile._id === user._id ? 'Delete' : 'Hide'}</button>
                          )}
                        </>
                      )}
                      {userProfile?._id === postOwnerId && (
                        <button
                          type="button"
                          aria-label={item.pinned ? 'Unpin comment' : 'Pin comment'}
                          disabled={pending === `comment-pin-${item._key}`}
                          onClick={() => onPin(item._key, !item.pinned)}
                        >
                          {item.pinned ? <BsPinAngleFill /> : <BsPinAngle />}
                          {item.pinned ? 'Unpin' : 'Pin'}
                        </button>
                      )}
                    </div>

                    {editing === `comment:${item._key}` && (
                      <form className="reply-form" onSubmit={async (event) => {
                        event.preventDefault()
                        await onEditComment(item._key, editText.trim())
                        setEditing('')
                      }}>
                        <input aria-label="Edit comment" maxLength={500} value={editText} onChange={(event) => setEditText(event.target.value)} />
                        <button disabled={!editText.trim()}>Save</button>
                        <button type="button" onClick={() => setEditing('')}>Cancel</button>
                      </form>
                    )}

                    {item.replies?.length > 0 && (
                      <div className="reply-list">
                        {item.replies.map((reply) => {
                          const replyUser = userFor(reply.postedBy)
                          return (
                            <div className="reply-item" key={reply._key}>
                              <Avatar src={replyUser.image} className="reply-avatar" alt="" />
                              <div>
                                <Link to={`/profile/${encodeURIComponent(replyUser._id)}`}>
                                  <strong>{replyUser.userName}</strong>
                                </Link>
                                <p><RichText text={reply.comment} /></p>
                                {reply.editedAt && <small className="edited-label">Edited</small>}
                                {userProfile && (
                                  <div className="reply-actions">
                                    {userProfile._id === replyUser._id && <button type="button" onClick={() => { setEditing(`reply:${reply._key}`); setEditText(reply.comment) }}>Edit</button>}
                                    {(userProfile._id === replyUser._id || userProfile._id === postOwnerId) && <button type="button" className="danger-text" onClick={() => {
                                      if (window.confirm('Remove this reply?')) onDeleteReply(item._key, reply._key)
                                    }}>Delete</button>}
                                  </div>
                                )}
                                {editing === `reply:${reply._key}` && (
                                  <form className="reply-form" onSubmit={async (event) => {
                                    event.preventDefault()
                                    await onEditReply(item._key, reply._key, editText.trim())
                                    setEditing('')
                                  }}>
                                    <input aria-label="Edit reply" maxLength={500} value={editText} onChange={(event) => setEditText(event.target.value)} />
                                    <button disabled={!editText.trim()}>Save</button>
                                    <button type="button" onClick={() => setEditing('')}>Cancel</button>
                                  </form>
                                )}
                              </div>
                            </div>
                          )
                        })}
                      </div>
                    )}

                    {replyingTo === item._key && (
                      <form className="reply-form" onSubmit={(event) => submitReply(event, item._key)}>
                        <input
                          autoFocus
                          aria-label={`Reply to ${user.userName}`}
                          maxLength={500}
                          value={replyText}
                          onChange={(event) => setReplyText(event.target.value)}
                          placeholder={`Reply to ${user.userName}...`}
                        />
                        <button disabled={!replyText.trim() || pending === `reply-${item._key}`}>
                          {pending === `reply-${item._key}` ? 'Sending...' : 'Reply'}
                        </button>
                      </form>
                    )}
                  </div>
                </div>
              </article>
            )
          })
        ) : (
          <NoResults text="No Comments Yet" />
        )}
      </div>
      {userProfile && (
        <form onSubmit={addComment} className="comment-composer">
          <input
            value={comment}
            maxLength={500}
            onChange={(event) => setComment(event.target.value)}
            placeholder="Add a comment..."
            aria-label="Add comment"
          />
          <button disabled={pending === 'comment' || !comment.trim()}>
            {pending === 'comment' ? 'Commenting...' : 'Comment'}
          </button>
        </form>
      )}
    </section>
  )
}

export default Comments
