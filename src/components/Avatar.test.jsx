import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'

import Avatar from './Avatar'

describe('Avatar', () => {
  it('loads remote profile images without a referrer', () => {
    render(<Avatar src="https://lh3.googleusercontent.com/example" alt="Student profile" />)

    const image = screen.getByRole('img', { name: 'Student profile' })
    expect(image).toHaveAttribute('src', 'https://lh3.googleusercontent.com/example')
    expect(image).toHaveAttribute('referrerpolicy', 'no-referrer')
  })

  it('shows the local avatar when a remote image fails', () => {
    render(<Avatar src="https://example.invalid/missing.jpg" alt="Student profile" />)
    const image = screen.getByRole('img', { name: 'Student profile' })

    fireEvent.error(image)

    expect(image).toHaveAttribute('src', '/avatar.svg')
  })
})
