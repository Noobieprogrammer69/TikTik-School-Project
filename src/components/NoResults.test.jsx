import { render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'

import NoResults from './NoResults'

describe('NoResults', () => {
  it('shows the supplied empty-state message', () => {
    render(<NoResults text="No Videos" />)
    expect(screen.getByRole('status')).toHaveTextContent('No Videos')
  })

  it('preserves the comments empty state', () => {
    render(<NoResults text="No Comments Yet" />)
    expect(screen.getByRole('status')).toHaveTextContent('No Comments Yet')
  })
})

