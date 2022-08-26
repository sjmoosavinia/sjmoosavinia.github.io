describe('empty spec', () => {
  it('passes', () => {
    // State set up
    cy.visit('https://sjmoosavinia.github.io')

    cy.get('.caption').click()
  })
})